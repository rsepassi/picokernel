# VirtIO Block and Network Device Support

## Overview

This document outlines the design for adding virtio-blk (block device) and
virtio-net (network device) support to VMOS, following the existing async work
queue pattern established for RNG and timer operations.

## API Design

### Block Device API

Block devices provide persistent storage with sector-based I/O. VirtIO block
devices report their sector size (typically 512 or 4096 bytes) which we detect
during device initialization.

#### Operation Types

Add to `kwork_op_t` in `kapi.h`:
```c
KWORK_OP_BLOCK_READ = 3,   // Block device read
KWORK_OP_BLOCK_WRITE = 4,  // Block device write
KWORK_OP_BLOCK_FLUSH = 5,  // Block device flush/fsync
```

#### Scatter-Gather Segment Structure

```c
// I/O segment for scatter-gather operations
typedef struct {
  uint64_t sector;           // Starting sector number
  uint8_t *buffer;           // Data buffer (must be 4K-aligned)
  size_t num_sectors;        // Number of sectors to transfer
  size_t completed_sectors;  // Sectors actually transferred (for partial I/O)
} kblk_segment_t;
```

#### Request Structure

```c
// Block request structure
typedef struct {
  kwork_t work;              // Embedded work item
  kblk_segment_t *segments;  // Array of I/O segments
  size_t num_segments;       // Number of segments
  kblk_req_platform_t platform; // Platform-specific fields
} kblk_req_t;
```

**Design rationale**:
- Uses `work.op` to distinguish READ/WRITE/FLUSH operations
- Scatter-gather support via segment array for efficient multi-block I/O
- Each segment tracks its own completion for partial transfers
- **All buffers must be 4K-aligned** (enforced at submission time)
- Implementations can start by supporting `num_segments = 1` and return
  `KERR_INVALID` for larger values
- Follows existing pattern: embedded `kwork_t`, platform extension

**Usage pattern (single segment)**:
```c
kblk_segment_t seg = {
  .sector = 0,
  .buffer = aligned_buffer,  // Must be 4K-aligned
  .num_sectors = 8,
  .completed_sectors = 0,
};

kblk_req_t req = {0};
req.work.op = KWORK_OP_BLOCK_READ;
req.work.callback = my_read_complete;
req.work.ctx = userdata;
req.segments = &seg;
req.num_segments = 1;
ksubmit(kernel, &req.work);
```

**Usage pattern (scatter-gather, future)**:
```c
kblk_segment_t segs[3] = {
  { .sector = 0, .buffer = buf0, .num_sectors = 8 },
  { .sector = 100, .buffer = buf1, .num_sectors = 16 },
  { .sector = 200, .buffer = buf2, .num_sectors = 8 },
};

kblk_req_t req = {0};
req.work.op = KWORK_OP_BLOCK_READ;
req.work.callback = my_read_complete;
req.work.ctx = userdata;
req.segments = segs;
req.num_segments = 3;  // Implementations may error if > 1
ksubmit(kernel, &req.work);
```

**Flush operation**:
```c
kblk_req_t req = {0};
req.work.op = KWORK_OP_BLOCK_FLUSH;
req.work.callback = flush_complete;
req.segments = NULL;  // Flush has no segments
req.num_segments = 0;
ksubmit(kernel, &req.work);
```

### Network Device API

Network devices provide packet I/O. VirtIO network devices support both transmit
(send) and receive operations. The MAC address is detected during device
initialization.

#### Operation Types

Add to `kwork_op_t` in `kapi.h`:
```c
KWORK_OP_NET_RECV = 6,  // Network packet receive
KWORK_OP_NET_SEND = 7,  // Network packet send
```

#### Network Buffer Structure

```c
// Network buffer for send/receive operations
typedef struct {
  uint8_t *buffer;           // Packet buffer
  size_t buffer_size;        // Buffer capacity (recv) or packet size (send)
  size_t packet_length;      // Actual packet length (filled on completion)
} knet_buffer_t;
```

#### Request Structures

```c
// Network receive request structure (ring buffer semantics)
typedef struct {
  kwork_t work;              // Embedded work item
  knet_buffer_t *buffers;    // Array of receive buffers (ring)
  size_t num_buffers;        // Number of buffers in ring
  size_t buffer_index;       // Which buffer was filled (set on completion)
  knet_req_platform_t platform; // Platform-specific fields
} knet_recv_req_t;

// Network send request structure
typedef struct {
  kwork_t work;              // Embedded work item
  knet_buffer_t *packets;    // Array of packets to send
  size_t num_packets;        // Number of packets
  size_t packets_sent;       // Number of packets actually sent
  knet_req_platform_t platform; // Platform-specific fields
} knet_send_req_t;
```

**Design rationale**:
- **Receive**: Ring buffer semantics. Submit once with N buffers, callback
  fires for each arriving packet with `buffer_index` set. Work remains LIVE
  until `kcancel()` is called. User must explicitly release buffers back to the
  ring via `knet_buffer_release()`. When all buffers are in use, packets are
  dropped.
  - **Implementation note**: Recv uses `KWORK_FLAG_STANDING` to keep work LIVE.
    The async runtime automatically transitions `READY → LIVE` after each
    callback, avoiding manual resubmission. On cancellation, callback fires
    with `KERR_CANCELLED` before work transitions to `DEAD`.
- **Send**: Batch multiple outgoing packets in one submission for efficiency.
  `packets_sent` tracks how many were successfully transmitted. Work becomes
  DEAD after completion (one-shot).
- Separate structures for send/recv (different semantics)
- Follows existing pattern: embedded `kwork_t`, platform extension

#### Helper API

```c
// Release a receive buffer back to the ring after processing
void knet_buffer_release(kernel_t *k, knet_recv_req_t *req, size_t buffer_index);

// Stop listening for packets and deregister all buffers
// (This is just kcancel, but shown for clarity)
kerr_t knet_recv_stop(kernel_t *k, knet_recv_req_t *req);
// Equivalent to: kcancel(k, &req->work);
```

**Usage pattern (receive with ring buffer)**:
```c
// Allocate receive buffer ring
static knet_buffer_t rx_bufs[4];
for (int i = 0; i < 4; i++) {
  rx_bufs[i].buffer = rx_buffers[i];
  rx_bufs[i].buffer_size = 1514;  // Standard Ethernet MTU
}

// Start receiving (submit once, stays LIVE)
static knet_recv_req_t recv_req = {0};
recv_req.work.op = KWORK_OP_NET_RECV;
recv_req.work.flags = KWORK_FLAG_STANDING;  // Keep work LIVE
recv_req.work.callback = packet_received;
recv_req.work.ctx = userdata;
recv_req.buffers = rx_bufs;
recv_req.num_buffers = 4;  // 4 buffers in ring
ksubmit(kernel, &recv_req.work);

// Callback fires for EACH packet (work stays LIVE)
void packet_received(kwork_t *work) {
  knet_recv_req_t *req = CONTAINER_OF(work, knet_recv_req_t, work);
  if (req->work.result == KERR_OK) {
    // Process the packet that was filled
    knet_buffer_t *buf = &req->buffers[req->buffer_index];
    process_packet(buf->buffer, buf->packet_length);

    // Release buffer back to ring (NOT resubmit!)
    knet_buffer_release(get_kernel(), req, req->buffer_index);
  }
}

// Later: stop listening and deregister buffers
kcancel(kernel, &recv_req.work);  // or knet_recv_stop()
```

**Usage pattern (send multiple packets)**:
```c
knet_buffer_t tx_packets[2] = {
  { .buffer = packet1, .buffer_size = 60 },
  { .buffer = packet2, .buffer_size = 128 },
};

knet_send_req_t send_req = {0};
send_req.work.op = KWORK_OP_NET_SEND;
send_req.work.callback = send_complete;
send_req.work.ctx = userdata;
send_req.packets = tx_packets;
send_req.num_packets = 2;
ksubmit(kernel, &send_req.work);

void send_complete(kwork_t *work) {
  knet_send_req_t *req = CONTAINER_OF(work, knet_send_req_t, work);
  if (req->work.result == KERR_OK) {
    printk("Sent %zu packets\n", req->packets_sent);
  }
}
```

## Refactoring Interrupts to Handle Multiple Devices

### Problem Statement

The current interrupt handling uses per-device flags (`irq_pending`) that are
checked in `platform_tick()`. With only VirtIO-RNG, this works fine:

```c
void platform_tick(platform_t *platform, kernel_t *k) {
  if (platform->virtio_rng_ptr && platform->virtio_rng_ptr->irq_pending) {
    virtio_rng_process_irq(platform->virtio_rng_ptr, k);
  }
}
```

However, with multiple devices (RNG, block, network), this approach has serious
scalability issues:

1. **O(n) flag scanning**: Must check every device flag on every `platform_tick()`
   call, even when no interrupts occurred
2. **Hardcoded device types**: Must modify `platform_tick()` for each new device
3. **No arrival ordering**: Always processes RNG first, regardless of which
   device actually interrupted

### Solution: Lock-Free IRQ Ring Buffer
use a lock-free queue to record which devices have pending interrupts.
SPSC (Single-Producer-Single-Consumer) ring buffer pattern.

#### Ring Buffer Structure and API

The ring buffer uses a clean API abstraction that hides the memory ordering
implementation details. This allows different platforms to choose between
volatile+barriers or C11 atomics without changing the usage code.

*Note: All the following will have a k prefix but elided here*

**API Definition** (in `src/irq_ring.h`):

```c
#ifndef IRQ_RING_H
#define IRQ_RING_H

#include <stdbool.h>
#include <stdint.h>

// Ring buffer size (power of 2 recommended for efficient modulo)
#define IRQ_RING_SIZE 256

// IRQ ring buffer structure (opaque to users)
typedef struct irq_ring irq_ring_t;

// Initialize the ring buffer
// Must be called before any other operations
void irq_ring_init(irq_ring_t *ring);

// Enqueue a device pointer (called from ISR)
// Returns: true on success, false if ring is full (overflow)
// On overflow, the overflow counter is incremented automatically
bool irq_ring_enqueue(irq_ring_t *ring, void *device);

// Dequeue a device pointer (called from platform_tick)
// Returns: device pointer, or NULL if ring is empty
void *irq_ring_dequeue(irq_ring_t *ring);

// Check if ring is empty (read_pos == write_pos)
bool irq_ring_is_empty(const irq_ring_t *ring);

// Get overflow counter (number of dropped interrupts)
uint32_t irq_ring_overflow_count(const irq_ring_t *ring);

#endif // IRQ_RING_H
```

**Structure Definition** (in `src/irq_ring.h`, after API):

```c
// Select implementation: 0 = volatile+barriers, 1 = C11 atomics
#ifndef IRQ_RING_USE_ATOMICS
#define IRQ_RING_USE_ATOMICS 0
#endif

#if IRQ_RING_USE_ATOMICS
#include <stdatomic.h>
struct irq_ring {
  void *items[IRQ_RING_SIZE];
  _Atomic uint32_t write_pos;      // ISR writes (producer)
  _Atomic uint32_t read_pos;       // platform_tick reads (consumer)
  _Atomic uint32_t overflow_count; // Dropped interrupt counter
};
#else
struct irq_ring {
  void *items[IRQ_RING_SIZE];
  volatile uint32_t write_pos;      // ISR writes (producer)
  volatile uint32_t read_pos;       // platform_tick reads (consumer)
  volatile uint32_t overflow_count; // Dropped interrupt counter
};
#endif
```

**Implementation** (in `src/irq_ring.c`):

```c
#include "irq_ring.h"

void irq_ring_init(irq_ring_t *ring) {
  ring->write_pos = 0;
  ring->read_pos = 0;
  ring->overflow_count = 0;
}

bool irq_ring_enqueue(irq_ring_t *ring, void *device) {
#if IRQ_RING_USE_ATOMICS
  // C11 atomics implementation (acquire/release ordering)
  uint32_t write_pos = atomic_load_explicit(&ring->write_pos, memory_order_relaxed);
  uint32_t read_pos = atomic_load_explicit(&ring->read_pos, memory_order_acquire);
  uint32_t next_write = write_pos + 1;

  // Check for overflow
  if (next_write - read_pos >= IRQ_RING_SIZE) {
    atomic_fetch_add_explicit(&ring->overflow_count, 1, memory_order_relaxed);
    return false;
  }

  // Store device pointer
  ring->items[write_pos % IRQ_RING_SIZE] = device;

  // Publish write (release semantics ensures item is visible)
  atomic_store_explicit(&ring->write_pos, next_write, memory_order_release);
  return true;

#else
  // Volatile + barriers implementation
  uint32_t write_pos = ring->write_pos;
  uint32_t read_pos = ring->read_pos;
  uint32_t next_write = write_pos + 1;

  // Check for overflow
  if (next_write - read_pos >= IRQ_RING_SIZE) {
    ring->overflow_count++;
    return false;
  }

  // Store device pointer
  ring->items[write_pos % IRQ_RING_SIZE] = device;

  // Full memory barrier (ensures item is visible before updating write_pos)
  __sync_synchronize();

  // Update write position
  ring->write_pos = next_write;
  return true;
#endif
}

void *irq_ring_dequeue(irq_ring_t *ring) {
#if IRQ_RING_USE_ATOMICS
  // C11 atomics implementation
  uint32_t read_pos = atomic_load_explicit(&ring->read_pos, memory_order_relaxed);
  uint32_t write_pos = atomic_load_explicit(&ring->write_pos, memory_order_acquire);

  // Check if empty
  if (read_pos == write_pos) {
    return NULL;
  }

  // Load device pointer (acquire ensures it's visible)
  void *device = ring->items[read_pos % IRQ_RING_SIZE];

  // Advance read position
  atomic_store_explicit(&ring->read_pos, read_pos + 1, memory_order_release);
  return device;

#else
  // Volatile + barriers implementation
  uint32_t read_pos = ring->read_pos;
  uint32_t write_pos = ring->write_pos;

  // Check if empty
  if (read_pos == write_pos) {
    return NULL;
  }

  // Load device pointer (dependency ordering via read_pos != write_pos)
  void *device = ring->items[read_pos % IRQ_RING_SIZE];

  // Advance read position (no barrier needed here - consumer is single-threaded)
  ring->read_pos = read_pos + 1;
  return device;
#endif
}

bool irq_ring_is_empty(const irq_ring_t *ring) {
#if IRQ_RING_USE_ATOMICS
  uint32_t read_pos = atomic_load_explicit(&ring->read_pos, memory_order_relaxed);
  uint32_t write_pos = atomic_load_explicit(&ring->write_pos, memory_order_acquire);
  return read_pos == write_pos;
#else
  return ring->read_pos == ring->write_pos;
#endif
}

uint32_t irq_ring_overflow_count(const irq_ring_t *ring) {
#if IRQ_RING_USE_ATOMICS
  return atomic_load_explicit(&ring->overflow_count, memory_order_relaxed);
#else
  return ring->overflow_count;
#endif
}
```

**Usage in platform_t** (in each platform's `platform_impl.h`):

```c
#include "irq_ring.h"

struct platform_t {
  // ... existing fields ...
  irq_ring_t irq_ring;
};
```

**Switching implementations**: Set `IRQ_RING_USE_ATOMICS` in platform config:
- `0`: Volatile + barriers (default, simpler, works everywhere)
- `1`: C11 atomics (more precise, requires C11 compiler support)

**API Benefits**:
1. **Clean abstraction**: ISR and platform_tick code is simple and readable
2. **Implementation agnostic**: Switch between volatile+barriers and atomics
   without changing usage code
3. **Encapsulated complexity**: Memory barriers and overflow logic hidden in API
4. **Type safety**: Functions use proper bool return types and const correctness
5. **Testable**: API can be unit tested independently of platform code

#### Atomics vs Volatile

Start with an impl just based on volatile + barriers. We can add C11 atomics later.

#### Device Base Structure

To enable generic device dispatch, all device structures must embed a
common header as their first field:

```c
// Device type enumeration
typedef enum {
  VIRTIO_DEVICE_RNG = 1,
  VIRTIO_DEVICE_BLK = 2,
  VIRTIO_DEVICE_NET = 3,
} kdevice_type_t;

// Common device header (MUST be first field in all device structures)
typedef struct {
  virtio_device_type_t device_type; // Device type tag
  platform_t *platform;             // Back-pointer for ISR access
  void (*process_irq)(void *dev, kernel_t *k);  // Virtual dispatch function
} kdevice_base_t;

// Example: RNG device embeds base as first member
typedef struct virtio_rng_dev {
  kdevice_base_t base;  // MUST be first for pointer casting!

  void *transport;
  int transport_type;
  virtqueue_t vq;
  // ... other RNG-specific fields ...

  // Remove: volatile uint8_t irq_pending;  // No longer needed!
} virtio_rng_dev_t;

// Similarly for block and network devices
typedef struct virtio_blk_dev {
  kdevice_base_t base;  // MUST be first!
  // ... block-specific fields ...
} virtio_blk_dev_t;

typedef struct virtio_net_dev {
  kdevice_base_t base;  // MUST be first!
  // ... network-specific fields ...
} virtio_net_dev_t;
```

**Key design**: The base structure is the first field, so
`(kdevice_base_t *)dev_ptr` and `(virtio_rng_dev_t *)dev_ptr` point to
the same address. This enables safe upcast/downcast without offset arithmetic.

#### ISR Changes (Enqueue Device)

Modify interrupt handlers to enqueue device pointers using the API:

```c
static void virtio_rng_irq_handler(void *context) {
  virtio_rng_dev_t *rng = (virtio_rng_dev_t *)context;
  platform_t *platform = rng->base.platform;

  // 1. Acknowledge hardware interrupt (device-specific, read ISR register)
  if (rng->transport_type == VIRTIO_TRANSPORT_PCI) {
    virtio_pci_read_isr((virtio_pci_transport_t *)rng->transport);
  } else {
    uint32_t isr = virtio_mmio_read_isr((virtio_mmio_transport_t *)rng->transport);
    virtio_mmio_ack_isr((virtio_mmio_transport_t *)rng->transport, isr);
  }

  // 2. Enqueue device pointer (handles overflow automatically)
  // Returns false if ring is full - interrupt is dropped, counter incremented
  irq_ring_enqueue(&platform->irq_ring, rng);

  // GIC/PIC EOI sent by irq_dispatch() or exception_handler()
}
```

**Critical**: The API handles overflow gracefully - `irq_ring_enqueue()` returns
false and increments the overflow counter. Lost interrupts are preferable to
kernel crashes. Memory barriers and atomic operations are handled internally.

#### platform_tick Changes (Dequeue and Dispatch)

Generic processing loop using the API and function pointer dispatch:

```c
void platform_tick(platform_t *platform, kernel_t *k) {
  // Process all pending device IRQs in arrival order (FIFO)
  void *dev_ptr;
  while ((dev_ptr = irq_ring_dequeue(&platform->irq_ring)) != NULL) {
    // Cast to device base pointer
    kdevice_base_t *dev = (kdevice_base_t *)dev_ptr;

    // Call device-specific interrupt handler via function pointer (vtable)
    // This calls virtio_rng_process_irq(), virtio_blk_process_irq(), etc.
    dev->process_irq(dev, k);
  }

  // Optional: Log overflow counter periodically for debugging
  uint32_t overflows = irq_ring_overflow_count(&platform->irq_ring);
  if (overflows > 0) {
    // Note: Can't call printk from here every time due to performance
    // Consider logging only on threshold or via separate diagnostic
    // e.g., if (overflows % 1000 == 0) printk("IRQ overflows: %u\n", overflows);
  }
}
```

**Benefits**:
- ✅ **Constant-time**: O(1) enqueue/dequeue, no device scanning
- ✅ **Generic**: No hardcoded device types, works for unlimited devices
- ✅ **Lock-free**: No mutexes needed (SPSC guarantees)
- ✅ **Arrival order**: Process interrupts in FIFO order
- ✅ **Scalable**: Add new devices without modifying `platform_tick()`

#### Device Initialization

During device setup, initialize the base structure:

```c
// In virtio_rng_init_*():
rng->base.device_type = VIRTIO_DEVICE_RNG;
rng->base.platform = platform;
rng->base.process_irq = virtio_rng_process_irq_dispatch;

// Wrapper function for type-safe dispatch:
static void virtio_rng_process_irq_dispatch(void *dev, kernel_t *k) {
  virtio_rng_dev_t *rng = (virtio_rng_dev_t *)dev;
  virtio_rng_process_irq(rng, k);  // Call actual device handler
}
```

Repeat for block and network devices with their respective handlers.

#### Memory Ordering Implementation Notes

These details are encapsulated within the `irq_ring_*` API but are documented
here for implementers and platform maintainers.

On ARM64 and RISC-V (weak memory models):
- Loads and stores can be reordered by CPU
- `volatile` prevents compiler reordering but NOT CPU reordering
- `__sync_synchronize()` is a full memory barrier (DMB on ARM, FENCE on RISC-V)
- C11 atomics provide acquire/release semantics for precise ordering

**Volatile + barriers approach** (write path in ISR):
```c
items[write_pos] = dev;        // Store data
__sync_synchronize();          // Full barrier (ensures data visible)
write_pos = next_pos;          // Store index (release)
```

**Volatile + barriers approach** (read path in platform_tick):
```c
while (read_pos != write_pos) {  // Load index (acquire)
  // Implicit barrier: the != comparison acts as a dependency
  dev = items[read_pos];         // Load data
  // Process...
  read_pos++;
}
```

**C11 atomics approach** provides more precise control:
```c
// Write: memory_order_release on write_pos ensures items[] is visible
atomic_store_explicit(&write_pos, next_pos, memory_order_release);

// Read: memory_order_acquire on write_pos ensures items[] is visible
uint32_t write = atomic_load_explicit(&write_pos, memory_order_acquire);
```

On x86-64, both approaches are efficient (TSO memory model makes barriers cheap
or no-ops).

#### Overflow Handling

**Ring overflow** occurs when `write_pos - read_pos >= IRQ_RING_SIZE`:

- **Do NOT panic**: This would make the system vulnerable to interrupt storms
- **Drop the interrupt**: Ack and drop, natural backpressure
- **Increment counter**: `overflow_count` tracks dropped interrupts
- **Log periodically**: Warn (from main event loop) if counter increases (indicates system overload)

**Ring sizing**: `IRQ_RING_SIZE = 256` handles 256 queued interrupts, far more
than expected under normal load. If overflow occurs frequently, the system is
either:
1. Not calling `platform_tick()` fast enough (long-running tasks)
2. Experiencing an interrupt storm (hardware issue)
3. Ring size is too small (increase to 512 or 1024)

#### Migration from Flag-Based to Ring-Based

**Phase 1**: Add ring buffer infrastructure
1. Add `irq_ring_t` to `platform_t`
2. Add `kdevice_base_t` to device structures
3. Initialize ring in `platform_init()`

**Phase 2**: Migrate VirtIO-RNG (reference implementation)
1. Add `base` field to `virtio_rng_dev_t`
2. Initialize base fields during device setup
3. Modify `virtio_rng_irq_handler()` to enqueue instead of setting flag
4. Update `platform_tick()` to dequeue and dispatch
5. Remove `irq_pending` flag from `virtio_rng_dev_t`
6. Test thoroughly

**Phase 3**: Add block and network devices
1. Add `base` field to `virtio_blk_dev_t` and `virtio_net_dev_t`
2. Implement ISR handlers using enqueue pattern
3. Implement `virtio_blk_process_irq()` and `virtio_net_process_irq()`
4. No changes needed to `platform_tick()` (already generic!)

## Device Configuration Storage

During device discovery, we detect and store device configuration in the
platform state. For now, we assume at most one block device and one network
device per system.

### Platform State Extensions

Add to `platform_t` (in each platform's `platform_impl.h`):

```c
typedef struct {
  // ... existing platform state ...

  // Block device info (valid if has_block_device = true)
  bool has_block_device;
  uint32_t block_sector_size;    // Detected sector size (512, 4096, etc.)
  uint64_t block_capacity;       // Total sectors

  // Network device info (valid if has_net_device = true)
  bool has_net_device;
  uint8_t net_mac_address[6];    // MAC address from device config

  // Device structures
  // ... platform-specific virtio device state ...
} platform_t;
```

On device discovery, log configuration using KLOG and manual formatting:
```c
KLOG("virtio-blk discovered");
printk("  sector_size=");
printk_u32(platform.block_sector_size);
printk(" capacity=");
printk_u64(platform.block_capacity);
printk(" sectors (");
printk_u64((platform.block_capacity * platform.block_sector_size) / (1024*1024));
printk(" MB)\n");

KLOG("virtio-net discovered");
printk("  mac=");
printk_hex8(platform.net_mac_address[0]); printk(":");
printk_hex8(platform.net_mac_address[1]); printk(":");
printk_hex8(platform.net_mac_address[2]); printk(":");
printk_hex8(platform.net_mac_address[3]); printk(":");
printk_hex8(platform.net_mac_address[4]); printk(":");
printk_hex8(platform.net_mac_address[5]); printk("\n");
```

Note: You may need to add `printk_u32`, `printk_u64`, and `printk_hex8` helper
functions if they don't already exist.

## Platform-Specific Extensions

Following the pattern from `krng_req_platform_t`, each platform will define:

```c
// In platform_impl.h for each platform
typedef struct {
  uint16_t desc_head;  // VirtIO descriptor chain head index
  // Other platform-specific tracking
} kblk_req_platform_t;

typedef struct {
  uint16_t desc_head;  // VirtIO descriptor chain head index
  // Other platform-specific tracking
} knet_req_platform_t;
```

## Error Handling

Reuse existing `kerr_t` codes and add new ones:

```c
// Existing codes
#define KERR_OK 0           // Success
#define KERR_BUSY 1         // Resource busy
#define KERR_INVALID 2      // Invalid argument
#define KERR_CANCELLED 3    // Operation cancelled
#define KERR_TIMEOUT 4      // Timeout
#define KERR_NO_DEVICE 5    // Device not available

// New codes
#define KERR_IO_ERROR 6     // I/O error (bad sector, transmission failure)
#define KERR_NO_SPACE 7     // Device full (block) or queue full (network)
```

### Error Usage

- `KERR_INVALID`: Invalid sector, unaligned buffer, num_segments > supported
- `KERR_NO_DEVICE`: Block/network device not present
- `KERR_IO_ERROR`: Hardware I/O error, bad sector
- `KERR_NO_SPACE`: Virtqueue full, cannot submit work
- `KERR_CANCELLED`: Operation cancelled via `kcancel()`

## Buffer Alignment Requirements

**All block I/O buffers MUST be 4K-aligned.** This is enforced at submission
time. Misaligned buffers will return `KERR_INVALID`.

Rationale:
- Matches page size on most architectures
- Efficient DMA operations
- Compatible with memory-mapped I/O
- Simplifies implementation

## Implementation Checklist

### Block Device (virtio-blk)

1. **Headers**:
   - [ ] `src/virtio_blk.h`: Device structures, descriptor layouts
   - [ ] Add `kblk_segment_t`, `kblk_req_t` to `src/kapi.h`
   - [ ] Add `KWORK_OP_BLOCK_READ/WRITE/FLUSH` to `kwork_op_t`
   - [ ] Add `KERR_IO_ERROR`, `KERR_NO_SPACE` to error codes

2. **Platform Implementation** (per platform):
   - [ ] Device discovery in `platform_virtio.c`
   - [ ] Detect and store sector size and capacity
   - [ ] Work submission handler for block operations (single segment only)
   - [ ] Validate 4K alignment, return `KERR_INVALID` if misaligned
   - [ ] Return `KERR_INVALID` if `num_segments > 1` (for now)
   - [ ] Interrupt handler for block completion
   - [ ] Add `has_block_device`, `block_sector_size`, `block_capacity` to
         `platform_t`

3. **User API** (optional helpers):
   - [ ] `kblk_read_init()`: Initialize single-segment read request
   - [ ] `kblk_write_init()`: Initialize single-segment write request
   - [ ] `kblk_flush_init()`: Initialize flush request

### Network Device (virtio-net)

1. **Headers**:
   - [ ] `src/virtio_net.h`: Device structures, descriptor layouts
   - [ ] Add `knet_buffer_t`, `knet_recv_req_t`, `knet_send_req_t` to
         `src/kapi.h`
   - [ ] Add `KWORK_OP_NET_RECV/SEND` to `kwork_op_t`

2. **Platform Implementation** (per platform):
   - [ ] Device discovery in `platform_virtio.c`
   - [ ] Detect and store MAC address
   - [ ] Work submission handler for network recv (multiple buffers)
   - [ ] Work submission handler for network send (multiple packets)
   - [ ] Interrupt handler for TX/RX completion
   - [ ] Separate virtqueues for RX and TX
   - [ ] Add `has_net_device`, `net_mac_address[6]` to `platform_t`

3. **User API** (optional helpers):
   - [ ] `knet_recv_init()`: Initialize receive request
   - [ ] `knet_send_init()`: Initialize send request

## Test Plan

### Block Device Test

Create a persistence test that validates read/write/flush operations and
persists across reboots.

**Magic bytes**: `0x564D4F53` ("VMOS" in ASCII)

**Test sequence**:
1. Read sector 0
2. Check for magic bytes at offset 0
3. If found:
   - Read timestamp at offset 4 (uint64_t)
   - Log: "Found existing magic: timestamp=%llu"
4. If not found:
   - Write magic bytes at offset 0
   - Log: "Writing new magic"
5. Get current time from platform
6. Write current timestamp at offset 4
7. Flush to ensure persistence
8. Read sector 0 again
9. Verify magic bytes and timestamp match
10. Log: "Verified magic and timestamp=%llu"

**Expected output**:
- First run: "Writing new magic", "Verified magic and timestamp=<T1>"
- Second run (same session): "Found existing magic: timestamp=<T1>",
  "Verified magic and timestamp=<T2>"
- After reboot: "Found existing magic: timestamp=<T2>", demonstrates
  persistence

**Code structure**:
```c
void test_block_device(kernel_t *k) {
  // Allocate 4K-aligned buffer
  static uint8_t __attribute__((aligned(4096))) sector_buffer[4096];

  // Step 1: Read sector 0
  kblk_segment_t seg = {
    .sector = 0,
    .buffer = sector_buffer,
    .num_sectors = 1,  // Assume sector_size <= 4096
  };
  kblk_req_t req = {0};
  req.work.op = KWORK_OP_BLOCK_READ;
  req.work.callback = on_read_complete;
  req.segments = &seg;
  req.num_segments = 1;
  ksubmit(k, &req.work);
}

void on_read_complete(kwork_t *work) {
  // Check magic, write timestamp, etc.
}
```

### Network Device Test

Create a UDP echo test with static networking configuration.

**Network Configuration** (QEMU user networking):
- Device IP: `10.0.2.15` (static)
- Gateway IP: `10.0.2.2` (static)
- Gateway MAC: `52:55:0a:00:02:02` (static)
- Device MAC: From virtio-net device config
- UDP Port: `8080`

**Protocol Stack** (minimal):
- Ethernet frame (14-byte header + payload)
- IPv4 header (20 bytes, no options)
- UDP header (8 bytes)
- Application data

**Test sequence**:
1. Register 4 receive buffers (1514 bytes each)
2. Wait for incoming UDP packet on port 8080
3. Parse Ethernet → IP → UDP headers
4. Log: "Received UDP packet from <src_ip>:<src_port> len=<N>"
5. Construct UDP response (swap src/dst)
6. Send response packet
7. Log: "Sent UDP response to <dst_ip>:<dst_port> len=<N>"

**Test data**: Simple ASCII string "Hello from VMOS!"

**Host-side test** (send UDP packet to VM):
```bash
# Send test packet
echo -n "ping" | nc -u 10.0.2.15 8080
```

**Code structure**:
```c
void test_network_device(kernel_t *k) {
  // Allocate receive buffers
  static uint8_t rx_buf0[1514], rx_buf1[1514], rx_buf2[1514], rx_buf3[1514];
  static knet_buffer_t rx_bufs[4] = {
    { .buffer = rx_buf0, .buffer_size = 1514 },
    { .buffer = rx_buf1, .buffer_size = 1514 },
    { .buffer = rx_buf2, .buffer_size = 1514 },
    { .buffer = rx_buf3, .buffer_size = 1514 },
  };

  knet_recv_req_t req = {0};
  req.work.op = KWORK_OP_NET_RECV;
  req.work.flags = KWORK_FLAG_STANDING;  // Keep work LIVE
  req.work.callback = on_packet_received;
  req.buffers = rx_bufs;
  req.num_buffers = 4;
  ksubmit(k, &req.work);
}

void on_packet_received(kwork_t *work) {
  knet_recv_req_t *req = CONTAINER_OF(work, knet_recv_req_t, work);
  knet_buffer_t *buf = &req->buffers[req->buffer_index];

  // Parse Ethernet, IP, UDP headers
  // Log packet info
  // Construct response
  // Send response

  // Release buffer back to ring (work automatically re-arms due to STANDING flag)
  knet_buffer_release(get_kernel(), req, req->buffer_index);
}
```

## Future Enhancements

### Timeout Support

Add optional timeout parameter to `ksubmit()`:
```c
kerr_t ksubmit_timeout(kernel_t *k, kwork_t *work, uint64_t timeout_ms);
```

Works generically across all operation types. Callback fires with
`KERR_TIMEOUT` if operation doesn't complete in time, if and only if operation
can be cancelled (timeout implies successful cancellation).

### Multi-Segment Block I/O

Once single-segment works, implement full scatter-gather:
- Modify platform implementation to handle `num_segments > 1`
- Use VirtIO descriptor chains efficiently
- Update per-segment `completed_sectors` on partial transfers

### Advanced Network Features

- ARP request/response handling (dynamic MAC resolution)
- DHCP client (dynamic IP configuration)
- Promiscuous mode / packet filtering

### Multiple Devices

Extend to support multiple block and network devices:
- Device enumeration API
- Per-device handles or IDs
- Device selection in work requests

# VMOS VirtIO Implementation

## Overview

VMOS uses **VirtIO** virtual devices for I/O operations. VirtIO is a standardized interface for virtual devices in hypervisors, designed for efficiency with paravirtualization. VMOS supports both **MMIO** and **PCI** transports.

**Supported Devices:**
- **VirtIO RNG** - Random Number Generator (hardware entropy)
- **VirtIO Block** - Block device (disk I/O)
- **VirtIO Network** - Network interface (packet send/receive)

**Key Characteristics:**
- Transport-agnostic device drivers (same driver works with MMIO or PCI)
- Virtqueue-based communication (shared memory rings)
- Interrupt-driven or polling
- Zero-copy DMA transfers

## Architecture

### Layered Structure

```
Device Drivers (virtio_rng.c, virtio_blk.c, virtio_net.c)
    │
    │ Uses virtqueue operations
    ▼
VirtIO Core (virtio.h, virtio.c)
    │
    │ Transport abstraction
    ▼
├─ MMIO Transport (virtio_mmio.c)    ├─ PCI Transport (virtio_pci.c)
│    MMIO register access             │    PCI BAR access, MSI-X
│    Direct memory mapping            │    Capability parsing
└────────────────────────────────────┴─────────────────────────────────
                         │
                         ▼
             Platform MMIO/PCI Functions (platform.h)
```

**Dependency principle:** Device drivers are transport-agnostic. They use `virtqueue_*` operations and transport function pointers, never directly accessing transport-specific details.

## Virtqueue

The **virtqueue** is the core communication mechanism between driver and device.

### Structure

A virtqueue consists of three rings in shared memory:

```
┌──────────────────────────────────────┐
│ Descriptor Table                     │  Driver and device both read/write
│ (Array of virtq_desc_t)              │  Contains buffer addresses & metadata
├──────────────────────────────────────┤
│ Available Ring (avail)               │  Driver writes, device reads
│ (virtq_avail_t + ring[])             │  Which descriptors are available
├──────────────────────────────────────┤
│ Padding (align to 4K)                │
├──────────────────────────────────────┤
│ Used Ring (used)                     │  Device writes, driver reads
│ (virtq_used_t + ring[])              │  Which descriptors are used (done)
└──────────────────────────────────────┘
```

**Location:** `driver/virtio/virtio.h:40-81`

### Descriptor

```c
typedef struct {
    uint64_t addr;  // Physical address
    uint32_t len;   // Length
    uint16_t flags; // VIRTQ_DESC_F_*
    uint16_t next;  // Next descriptor (if NEXT flag set)
} virtq_desc_t;
```

**Flags:**
- `VIRTQ_DESC_F_NEXT (1)`: Descriptor has next (chained descriptors)
- `VIRTQ_DESC_F_WRITE (2)`: Device writes to buffer (vs. driver writes)
- `VIRTQ_DESC_F_INDIRECT (4)`: Buffer contains list of descriptors

**Example:** For a block read:
1. Header descriptor (device writes)
2. Data descriptor (device writes, NEXT flag)
3. Status descriptor (device writes)

### Available Ring

```c
typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[]; // Variable size
} virtq_avail_t;
```

**Purpose:** Driver tells device which descriptor chains are available

**Flow:**
1. Driver allocates descriptors
2. Driver sets up descriptor chain
3. Driver adds head descriptor index to `ring[idx % queue_size]`
4. Driver increments `idx`
5. Driver optionally notifies device (kick)

### Used Ring

```c
typedef struct {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[]; // Variable size
} virtq_used_t;

typedef struct {
    uint32_t id;  // Descriptor chain head
    uint32_t len; // Bytes written
} virtq_used_elem_t;
```

**Purpose:** Device tells driver which descriptor chains are completed

**Flow:**
1. Device processes descriptor chain
2. Device adds entry to `ring[idx % queue_size]` with head ID and bytes written
3. Device increments `idx`
4. Device optionally sends interrupt

### Virtqueue Operations

**Initialize virtqueue:**
```c
void virtqueue_init(virtqueue_t *vq, uint16_t queue_size, void *base);
```

**Allocate descriptor:**
```c
uint16_t virtqueue_alloc_desc(virtqueue_t *vq);
// Returns: descriptor index or VIRTQUEUE_NO_DESC if full
```

**Setup descriptor:**
```c
void virtqueue_add_desc(virtqueue_t *vq, uint16_t idx, uint64_t addr,
                        uint32_t len, uint16_t flags);
```

**Add to available ring:**
```c
void virtqueue_add_avail(virtqueue_t *vq, uint16_t desc_idx);
```

**Check for used entries:**
```c
int virtqueue_has_used(virtqueue_t *vq);
// Returns: number of used entries
```

**Get used descriptor:**
```c
void virtqueue_get_used(virtqueue_t *vq, uint16_t *desc_idx, uint32_t *len);
```

**Free descriptor:**
```c
void virtqueue_free_desc(virtqueue_t *vq, uint16_t desc_idx);
```

**Location:** `driver/virtio/virtio.c`

### Memory Layout

VMOS pre-allocates virtqueue memory statically:

```c
#define VIRTQUEUE_MAX_SIZE 256

typedef struct {
    // Descriptor table (4096 bytes)
    virtq_desc_t descriptors[VIRTQUEUE_MAX_SIZE];

    // Available ring (518 bytes)
    struct {
        uint16_t flags;
        uint16_t idx;
        uint16_t ring[VIRTQUEUE_MAX_SIZE];
        uint16_t used_event;
    } __attribute__((packed)) available;

    // Padding to 4K alignment (3578 bytes)
    uint8_t padding[8192 - 4096 - 518];

    // Used ring (2054 bytes)
    struct {
        uint16_t flags;
        uint16_t idx;
        virtq_used_elem_t ring[VIRTQUEUE_MAX_SIZE];
        uint16_t avail_event;
    } __attribute__((packed)) used;
} __attribute__((aligned(4096))) virtqueue_memory_t;
```

**Total size:** ~12KB per virtqueue

**Storage:** Embedded in `platform_t` structure, one per device

**Location:** `driver/virtio/virtio.h:122-148`

## MMIO Transport

MMIO (Memory-Mapped I/O) transport accesses VirtIO devices through fixed MMIO regions.

### MMIO Registers

```c
#define VIRTIO_MMIO_MAGIC_VALUE      0x000  // Magic "virt" (0x74726976)
#define VIRTIO_MMIO_VERSION          0x004  // 1 = legacy, 2 = modern
#define VIRTIO_MMIO_DEVICE_ID        0x008  // Device type (4=RNG, 2=block, 1=net)
#define VIRTIO_MMIO_VENDOR_ID        0x00c  // Vendor ID
#define VIRTIO_MMIO_DEVICE_FEATURES  0x010  // Device features
#define VIRTIO_MMIO_DRIVER_FEATURES  0x020  // Driver features (write)
#define VIRTIO_MMIO_QUEUE_SEL        0x030  // Queue select
#define VIRTIO_MMIO_QUEUE_NUM_MAX    0x034  // Max queue size
#define VIRTIO_MMIO_QUEUE_NUM        0x038  // Queue size (write)
#define VIRTIO_MMIO_QUEUE_READY      0x044  // Queue ready (v2+)
#define VIRTIO_MMIO_QUEUE_NOTIFY     0x050  // Queue notify (kick)
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060  // Interrupt status
#define VIRTIO_MMIO_INTERRUPT_ACK    0x064  // Interrupt acknowledge
#define VIRTIO_MMIO_STATUS           0x070  // Device status
#define VIRTIO_MMIO_QUEUE_DESC_LOW   0x080  // Descriptor table address (low)
#define VIRTIO_MMIO_QUEUE_DESC_HIGH  0x084  // Descriptor table address (high)
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW 0x090  // Available ring address (low)
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH 0x094 // Available ring address (high)
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW 0x0a0  // Used ring address (low)
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH 0x0a4 // Used ring address (high)
```

**Location:** `driver/virtio/virtio_mmio.h:16-44`

### MMIO Device Discovery

**ARM/RISC-V (FDT-based):**
1. Parse device tree for `virtio,mmio` nodes
2. Extract `reg` property (base address, size)
3. Extract `interrupts` property (IRQ number)
4. Read magic value to verify device
5. Read device ID to determine type

**x64 (hardcoded ranges):**
1. Probe known MMIO addresses (e.g., 0x0a000000 on QEMU)
2. Check for magic value (0x74726976)
3. Read device ID

**Implementation:** `platform/*/platform_virtio.c`

### MMIO Initialization

```c
// 1. Initialize MMIO transport
virtio_mmio_transport_t mmio;
virtio_mmio_init(&mmio, (void *)mmio_base);

// 2. Reset device
virtio_mmio_reset(&mmio);

// 3. Set ACKNOWLEDGE status
virtio_mmio_set_status(&mmio, VIRTIO_STATUS_ACKNOWLEDGE);

// 4. Set DRIVER status
virtio_mmio_set_status(&mmio, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

// 5. Feature negotiation
uint32_t features = virtio_mmio_get_features(&mmio, 0);
virtio_mmio_set_features(&mmio, 0, accepted_features);

// 6. Set FEATURES_OK
virtio_mmio_set_status(&mmio, VIRTIO_STATUS_ACKNOWLEDGE |
                              VIRTIO_STATUS_DRIVER |
                              VIRTIO_STATUS_FEATURES_OK);

// 7. Setup virtqueue
virtio_mmio_setup_queue(&mmio, 0, &vq, queue_size);

// 8. Set DRIVER_OK
virtio_mmio_set_status(&mmio, VIRTIO_STATUS_ACKNOWLEDGE |
                              VIRTIO_STATUS_DRIVER |
                              VIRTIO_STATUS_FEATURES_OK |
                              VIRTIO_STATUS_DRIVER_OK);
```

**Location:** `driver/virtio/virtio_mmio.c`, device init functions

### MMIO Queue Notification

```c
void virtio_mmio_notify_queue(virtio_mmio_transport_t *mmio, uint16_t queue_idx) {
    platform_mmio_write32((volatile uint32_t *)(mmio->base + VIRTIO_MMIO_QUEUE_NOTIFY),
                          queue_idx);
}
```

**When to notify:** After adding buffers to available ring

## PCI Transport

PCI transport accesses VirtIO devices through PCI configuration space and BARs.

### PCI Device Discovery

1. Scan PCI configuration space (bus 0-255, slot 0-31, func 0-7)
2. Check for Vendor ID 0x1AF4 (VirtIO vendor)
3. Check for Device ID 0x1000-0x103F (VirtIO devices)
4. Read BARs to get MMIO regions
5. Parse PCI capabilities to find VirtIO structures

**Implementation:** `platform/*/platform_virtio.c`

### PCI Capabilities

VirtIO PCI devices use **PCI capabilities** to expose structures:

```c
#define VIRTIO_PCI_CAP_COMMON_CFG 1  // Common configuration
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2  // Queue notification
#define VIRTIO_PCI_CAP_ISR_CFG    3  // ISR status
#define VIRTIO_PCI_CAP_DEVICE_CFG 4  // Device-specific config
```

**Parsing capabilities:**
```c
int virtio_pci_init(virtio_pci_transport_t *pci, struct platform *platform,
                    uint8_t bus, uint8_t slot, uint8_t func) {
    // 1. Find capabilities pointer
    uint8_t cap_ptr = platform_pci_config_read8(platform, bus, slot, func, 0x34);

    // 2. Walk capability list
    while (cap_ptr != 0) {
        uint8_t cap_type = read_pci_cap_type(cap_ptr);

        if (cap_type == VIRTIO_PCI_CAP_COMMON_CFG) {
            // Map common config structure
        } else if (cap_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
            // Get notification base
        } else if (cap_type == VIRTIO_PCI_CAP_ISR_CFG) {
            // Map ISR status register
        }

        cap_ptr = read_next_cap_ptr(cap_ptr);
    }
}
```

**Location:** `driver/virtio/virtio_pci.c:virtio_pci_init()`

### PCI Common Configuration

```c
typedef struct {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_driver;
    uint64_t queue_device;
} __attribute__((packed)) virtio_pci_common_cfg_t;
```

**Access:** Direct MMIO writes to structure in BAR-mapped memory

**Location:** `driver/virtio/virtio_pci.h:22-40`

### MSI-X Support

**x64 platforms** use MSI-X for interrupts (more efficient than legacy INTx):

```c
// Configure MSI-X vectors
void virtio_pci_set_msix_vectors(virtio_pci_transport_t *pci,
                                  uint16_t config_vector,
                                  uint16_t queue_vector) {
    pci->msix_config_vector = config_vector;
    pci->msix_queue_vector = queue_vector;

    // Write to device
    pci->common_cfg->msix_config = config_vector;
    pci->common_cfg->queue_msix_vector = queue_vector;
}
```

**Other platforms** use legacy INTx (interrupt swizzling based on slot):
```c
uint32_t irq = platform_pci_irq_swizzle(platform, slot, int_pin);
```

**Location:** `driver/virtio/virtio_pci.c`, `platform/x64/platform_virtio.c`

## Device Drivers

### Common Device Pattern

All VirtIO device drivers follow this pattern:

```c
typedef struct {
    kdevice_base_t base;  // MUST be first field

    void *transport;      // MMIO or PCI transport
    int transport_type;   // VIRTIO_TRANSPORT_MMIO or _PCI

    virtqueue_t vq;       // Virtqueue
    virtqueue_memory_t *vq_memory;

    void *active_requests[MAX_REQUESTS];  // Request tracking
    kernel_t *kernel;     // Kernel reference
} virtio_xxx_dev_t;
```

**Key fields:**
- `base`: Device base (enables type-safe casting)
- `transport`: Points to either `virtio_mmio_transport_t` or `virtio_pci_transport_t`
- `transport_type`: Determines which transport functions to call
- `active_requests`: Maps descriptor index → request pointer

### VirtIO RNG (Random Number Generator)

**Device ID:** 4

**Purpose:** Hardware random number generation

**Virtqueues:** 1 (requestq)

**Request structure:**
```c
typedef struct {
    kwork_t work;
    uint8_t *buffer;
    size_t length;
    size_t completed;
    krng_req_platform_t platform;  // Contains desc_idx
} krng_req_t;
```

**Submission flow:**
```c
void virtio_rng_submit_work(virtio_rng_dev_t *rng, kwork_t *submissions,
                            kernel_t *k) {
    for (kwork_t *work = submissions; work != NULL; work = work->next) {
        krng_req_t *req = CONTAINER_OF(work, krng_req_t, work);

        // 1. Allocate descriptor
        uint16_t desc_idx = virtqueue_alloc_desc(&rng->vq);

        // 2. Setup descriptor (device writes)
        virtqueue_add_desc(&rng->vq, desc_idx,
                          (uint64_t)req->buffer, req->length,
                          VIRTQ_DESC_F_WRITE);

        // 3. Store request pointer for completion
        rng->active_requests[desc_idx] = req;
        req->platform.desc_idx = desc_idx;

        // 4. Add to available ring
        virtqueue_add_avail(&rng->vq, desc_idx);
    }

    // 5. Notify device
    notify_device(rng);
}
```

**Completion flow:**
```c
void virtio_rng_process_irq(virtio_rng_dev_t *rng, kernel_t *k) {
    while (virtqueue_has_used(&rng->vq)) {
        uint16_t desc_idx;
        uint32_t len;

        // 1. Get used descriptor
        virtqueue_get_used(&rng->vq, &desc_idx, &len);

        // 2. Recover request
        krng_req_t *req = rng->active_requests[desc_idx];
        req->completed = len;

        // 3. Free descriptor
        virtqueue_free_desc(&rng->vq, desc_idx);
        rng->active_requests[desc_idx] = NULL;

        // 4. Complete work
        kplatform_complete_work(k, &req->work, KERR_OK);
    }
}
```

**Location:** `driver/virtio/virtio_rng.c`, `driver/virtio/virtio_rng.h`

### VirtIO Block (Disk I/O)

**Device ID:** 2

**Purpose:** Block device (disk read/write)

**Virtqueues:** 1 (requestq)

**Request header:**
```c
struct virtio_blk_req_header {
    uint32_t type;        // VIRTIO_BLK_T_IN (read) or VIRTIO_BLK_T_OUT (write)
    uint32_t reserved;
    uint64_t sector;      // Starting sector
};
```

**Descriptor chain:**
1. Header (driver writes)
2. Data buffer (device writes for read, driver writes for write)
3. Status byte (device writes)

**Features:**
- Scatter-gather I/O (multiple buffers per request)
- Read, write, flush operations
- 512-byte or 4K sectors

**Location:** `driver/virtio/virtio_blk.c`, `driver/virtio/virtio_blk.h`

### VirtIO Network (Packet I/O)

**Device ID:** 1

**Purpose:** Network interface (packet send/receive)

**Virtqueues:** 2 (receiveq, transmitq)

**Packet header:**
```c
struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;  // v1 only
};
```

**Receive (standing work):**
```c
// Buffers stay in virtqueue, device fills them as packets arrive
kwork_init(&recv_req.work, KWORK_OP_NET_RECV, ctx, callback,
           KWORK_FLAG_STANDING);
```

**Send:**
```c
// Submit packet, device transmits and returns descriptor
kwork_init(&send_req.work, KWORK_OP_NET_SEND, ctx, callback, 0);
```

**Features:**
- MAC address configuration
- MTU negotiation
- Checksum offload (if negotiated)
- TSO/GSO support (if negotiated)

**Location:** `driver/virtio/virtio_net.c`, `driver/virtio/virtio_net.h`

## Device Initialization Sequence

### 1. Discovery

Platform discovers devices during `platform_init()`:

**MMIO:**
```c
platform_mmio_device_t devices[8];
int count = platform_discover_mmio_devices(platform, devices, 8);
```

**PCI:**
```c
for (bus = 0; bus < 256; bus++) {
    for (slot = 0; slot < 32; slot++) {
        uint16_t vendor = platform_pci_config_read16(platform, bus, slot, 0, 0);
        if (vendor == 0x1AF4) {  // VirtIO vendor
            // Initialize device
        }
    }
}
```

### 2. Transport Initialization

**MMIO:**
```c
virtio_mmio_init(&platform->virtio_mmio_transport_rng, mmio_base);
```

**PCI:**
```c
virtio_pci_init(&platform->virtio_pci_transport_rng, platform, bus, slot, func);
```

### 3. Device Initialization

```c
// RNG example
virtio_rng_init_mmio(&platform->virtio_rng,
                     &platform->virtio_mmio_transport_rng,
                     &platform->virtqueue_rng_memory,
                     kernel);
```

### 4. Interrupt Registration

```c
platform_irq_register(platform, irq_num, virtio_rng_irq_handler, rng_dev);
platform_irq_enable(platform, irq_num);
```

### 5. Set Pointer

```c
platform->virtio_rng_ptr = &platform->virtio_rng;  // Device is now active
```

## Adding a New VirtIO Device

To add support for a new VirtIO device (e.g., VirtIO Console):

### 1. Create Header File

`driver/virtio/virtio_console.h`:

```c
#pragma once
#include "virtio.h"

#define VIRTIO_ID_CONSOLE 3

typedef struct {
    kdevice_base_t base;
    void *transport;
    int transport_type;

    virtqueue_t vq_rx;  // Receive queue
    virtqueue_t vq_tx;  // Transmit queue
    virtqueue_memory_t *vq_rx_memory;
    virtqueue_memory_t *vq_tx_memory;

    // Device-specific state
    void *active_rx_requests[256];
    void *active_tx_requests[256];

    kernel_t *kernel;
} virtio_console_dev_t;

int virtio_console_init_mmio(virtio_console_dev_t *console,
                              virtio_mmio_transport_t *mmio,
                              virtqueue_memory_t *rx_memory,
                              virtqueue_memory_t *tx_memory,
                              kernel_t *kernel);

int virtio_console_init_pci(virtio_console_dev_t *console,
                             virtio_pci_transport_t *pci,
                             virtqueue_memory_t *rx_memory,
                             virtqueue_memory_t *tx_memory,
                             kernel_t *kernel);

void virtio_console_submit_work(virtio_console_dev_t *console,
                                 kwork_t *submissions, kernel_t *k);

void virtio_console_process_irq(virtio_console_dev_t *console, kernel_t *k);
```

### 2. Implement Driver

`driver/virtio/virtio_console.c`:

```c
#include "virtio_console.h"
#include "kernel.h"

int virtio_console_init_mmio(virtio_console_dev_t *console,
                              virtio_mmio_transport_t *mmio,
                              virtqueue_memory_t *rx_memory,
                              virtqueue_memory_t *tx_memory,
                              kernel_t *kernel) {
    // 1. Initialize base
    console->base.device_type = KDEVICE_TYPE_VIRTIO_CONSOLE;
    console->base.process_irq = (void (*)(void *, struct kernel *))virtio_console_process_irq;

    // 2. Store transport
    console->transport = mmio;
    console->transport_type = VIRTIO_TRANSPORT_MMIO;
    console->kernel = kernel;

    // 3. Reset device
    virtio_mmio_reset(mmio);

    // 4. Feature negotiation
    // ...

    // 5. Setup virtqueues
    virtqueue_init(&console->vq_rx, 256, rx_memory);
    virtio_mmio_setup_queue(mmio, 0, &console->vq_rx, 256);

    virtqueue_init(&console->vq_tx, 256, tx_memory);
    virtio_mmio_setup_queue(mmio, 1, &console->vq_tx, 256);

    // 6. Set DRIVER_OK
    virtio_mmio_set_status(mmio, VIRTIO_STATUS_DRIVER_OK);

    return 0;
}

void virtio_console_submit_work(virtio_console_dev_t *console,
                                 kwork_t *submissions, kernel_t *k) {
    // Process submissions, add to virtqueue
}

void virtio_console_process_irq(virtio_console_dev_t *console, kernel_t *k) {
    // Process used ring, complete work
}
```

### 3. Add to Platform

`platform/*/platform_impl.h`:

```c
struct platform {
    // ... existing fields

    virtio_console_dev_t virtio_console;
    virtqueue_memory_t virtqueue_console_rx_memory;
    virtqueue_memory_t virtqueue_console_tx_memory;
    virtio_console_dev_t *virtio_console_ptr;
};
```

### 4. Initialize in Platform

`platform/*/platform_virtio.c`:

```c
if (device_id == VIRTIO_ID_CONSOLE) {
    virtio_console_init_mmio(&platform->virtio_console,
                             &mmio_transport,
                             &platform->virtqueue_console_rx_memory,
                             &platform->virtqueue_console_tx_memory,
                             kernel);
    platform->virtio_console_ptr = &platform->virtio_console;
}
```

### 5. Handle in platform_submit()

```c
void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations) {
    // ... existing cases

    case KWORK_OP_CONSOLE_READ:
    case KWORK_OP_CONSOLE_WRITE:
        if (platform->virtio_console_ptr) {
            virtio_console_submit_work(platform->virtio_console_ptr,
                                       submissions, k);
        }
        break;
}
```

## Debugging

### Common Issues

**Device not found:**
- Check QEMU command line includes device (`-device virtio-rng-device` for MMIO or `-device virtio-rng-pci` for PCI)
- Verify device tree (FDT) contains device node
- Check PCI vendor ID (0x1AF4) and device ID

**Virtqueue initialization fails:**
- Verify memory alignment (4096 bytes)
- Check queue size is power of 2
- Ensure addresses are physical, not virtual

**Interrupts not firing:**
- Verify IRQ registered and enabled
- Check interrupt controller initialization
- Verify device status is DRIVER_OK
- For PCI: check MSI-X configuration

**Descriptor exhaustion:**
- Check `virtqueue_alloc_desc()` returns valid index
- Verify descriptors are freed in completion path
- Monitor `vq->num_free` (should increase after completion)

**Data corruption:**
- Verify buffer addresses are physical
- Check buffer alignment (some devices require 4K alignment)
- Ensure no buffer reuse before completion

### Diagnostic Functions

**Dump virtqueue state:**
```c
void virtqueue_dump(virtqueue_t *vq) {
    printk("Virtqueue: size=%u free=%u\n", vq->queue_size, vq->num_free);
    printk("  avail.idx=%u used.idx=%u last_used=%u\n",
           vq->avail->idx, vq->used->idx, vq->last_used_idx);
}
```

**Check device status:**
```c
uint8_t status = virtio_mmio_get_status(mmio);
printk("Device status: 0x%x\n", status);
// Should be 0x0F (ACKNOWLEDGE | DRIVER | FEATURES_OK | DRIVER_OK)
```

## Summary

The VirtIO subsystem provides:

✅ **Transport abstraction** - Same driver works with MMIO or PCI
✅ **Efficient I/O** - Zero-copy DMA via virtqueues
✅ **Standard interface** - VirtIO spec compliance
✅ **Multiple devices** - RNG, block, network support
✅ **Interrupt-driven** - Async completion via interrupts
✅ **Modular design** - Easy to add new device types

For more details:
- **Architecture**: See [architecture.md](architecture.md)
- **Async work**: See [async-work.md](async-work.md)
- **Platform API**: See [platform-api.md](platform-api.md)
- **VirtIO spec**: https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.html

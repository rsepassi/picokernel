# VirtIO in vmos

## Overview

vmos implements VirtIO device support with a three-layer architecture that
separates device drivers, transport mechanisms, and platform-specific code.
Currently supports VirtIO-RNG on both x64 (PCI) and arm64 (MMIO).

**Architecture layers:**
- **Device drivers** (`driver/virtio/`) - Transport-agnostic device logic
- **Transport implementations** - PCI (x64) and MMIO (arm64)
- **Platform integration** (`platform/*/`) - Device discovery, interrupts, cache coherency

## Code Organization

```
driver/virtio/
  virtio.h/c          - Virtqueue management (descriptors, rings)
  virtio_pci.h/c      - Generic PCI transport
  virtio_mmio.h/c     - Generic MMIO transport
  virtio_rng.h/c      - RNG device driver (transport-agnostic)

platform/x64/
  platform_virtio.c   - PCI device discovery, interrupt setup
  pci.c/h             - PCI configuration space access
  acpi_devices.c      - ACPI-based MMIO device discovery (fallback)

platform/arm64/
  virtio_mmio.c       - MMIO device discovery, GIC setup
  interrupt.c         - GIC interrupt configuration
```

## VirtIO Core Concepts

### Virtqueues

VirtIO uses virtqueues for device communication. Each virtqueue has three rings:

**Descriptor table** - Buffers available for device operations:
```c
typedef struct {
    uint64_t addr;     // Physical address
    uint32_t len;      // Buffer length
    uint16_t flags;    // VIRTQ_DESC_F_{NEXT,WRITE,INDIRECT}
    uint16_t next;     // Next descriptor if NEXT flag set
} virtq_desc_t;
```

**Available ring** - Driver writes descriptor indices here:
```c
typedef struct {
    uint16_t flags;
    uint16_t idx;      // Driver increments when adding descriptors
    uint16_t ring[];   // Descriptor indices
} virtq_avail_t;
```

**Used ring** - Device writes completion entries here:
```c
typedef struct {
    uint32_t id;       // Descriptor chain head
    uint32_t len;      // Bytes written by device
} virtq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;      // Device increments when processing descriptors
    virtq_used_elem_t ring[];
} virtq_used_t;
```

### Virtqueue Operations

Core operations in `driver/virtio/virtio.c`:
- `virtqueue_alloc_desc()` - Allocate descriptor from free list
- `virtqueue_add_desc()` - Setup descriptor (address, length, flags)
- `virtqueue_add_avail()` - Add descriptor to available ring
- `virtqueue_kick()` - Notify device of new descriptors
- `virtqueue_has_used()` - Check for completed descriptors
- `virtqueue_get_used()` - Get completed descriptor and length
- `virtqueue_free_desc()` - Return descriptor to free list

## Transport Mechanisms

### PCI Transport (x64)

Uses VirtIO PCI modern interface with capability-based configuration.

**Device discovery:**
- PCI bus scan (buses 0-3, slots 0-31)
- Vendor ID: 0x1AF4 (Red Hat)
- Device IDs: 0x1005 (legacy RNG), 0x1044 (modern RNG)
- Fallback: ACPI-based MMIO device discovery

**Capabilities:**
- `COMMON_CFG` - Device configuration and virtqueue setup
- `NOTIFY_CFG` - Virtqueue notification BAR and offset multiplier
- `ISR_CFG` - Interrupt status register

**Interrupts:**
- Legacy PCI interrupts (not MSI-X)
- Routed through LAPIC
- Deferred processing pattern (ISR sets flag, `platform_tick()` processes)

**Cache coherency:**
- Hardware-maintained on x86-64 (no explicit cache operations needed)

**Implementation:** `driver/virtio/virtio_pci.c`, `platform/x64/platform_virtio.c`

### MMIO Transport (arm64)

Uses VirtIO MMIO register interface for device access.

**Device discovery:**
- Fixed memory region scan: 0x0a000000 base
- 32 slots at 0x200 byte intervals
- Magic value: 0x74726976 ("virt")
- Supports both v1 (legacy) and v2 (modern) MMIO

**Register layout (key registers):**
```
0x000: Magic value
0x004: Version (1 = legacy, 2 = modern)
0x008: Device ID
0x030: Queue select
0x034: Queue num max
0x038: Queue num
0x050: Queue notify
0x060: Interrupt status
0x070: Device status
```

**Version differences:**
- **v1 (legacy):** Single contiguous queue memory, `QUEUE_PFN` register
- **v2 (modern):** Separate descriptor/available/used addresses

**Interrupts:**
- GIC SPIs (Shared Peripheral Interrupts)
- IRQs 48-79 for slots 0-31
- Edge-triggered interrupt configuration

**Cache coherency:**
- Manual cache maintenance required:
  - `dc cvac` (clean) before device reads
  - `dc ivac` (invalidate) before CPU reads
  - `dsb sy` memory barriers

**Implementation:** `driver/virtio/virtio_mmio.c`, `platform/arm64/virtio_mmio.c`

## Device Drivers

### VirtIO-RNG

Random number generator device (Device ID 4).

**Request tracking:**
- `active_requests[]` array indexed by descriptor index
- Constant-time O(1) lookup in interrupt handler
- Request structure:
```c
typedef struct {
    work_t work;         // Embedded work item
    uint8_t* buffer;     // Buffer to fill
    size_t length;       // Bytes requested
    size_t completed;    // Bytes actually read
    uint16_t desc_idx;   // Descriptor index
} krng_req_t;
```

**Work submission:**
1. Allocate descriptor from virtqueue
2. Setup descriptor with device-writable buffer (`VIRTQ_DESC_F_WRITE`)
3. Add to available ring
4. Track request in `active_requests[desc_idx]`
5. Kick device via transport notification
6. Mark work as `KWORK_STATE_LIVE`

**Backpressure:**
- Returns `KERR_BUSY` immediately when virtqueue is full
- Caller can retry submission

**Completion:**
1. Device processes request and writes to used ring
2. Device generates interrupt
3. ISR sets `irq_pending` flag
4. `platform_tick()` processes completions:
   - Read used ring entries
   - Lookup request via `active_requests[desc_idx]`
   - Call `kplatform_complete_work()` with result
   - Free descriptor

**Implementation:** `driver/virtio/virtio_rng.c`

### User Code Example

```c
static krng_req_t g_rng_req;
static uint8_t g_random_buf[32];

static void on_random_ready(kwork_t* work) {
    krng_req_t* req = CONTAINER_OF(work, krng_req_t, work);

    if (work->result != KERR_OK) {
        printk("RNG failed\n");
        return;
    }

    // Use random bytes in req->buffer
    printk("Got %d random bytes\n", req->completed);
}

void kusermain(kernel_t* k) {
    g_rng_req.work.op = KWORK_OP_RNG_READ;
    g_rng_req.work.callback = on_random_ready;
    g_rng_req.buffer = g_random_buf;
    g_rng_req.length = 32;

    ksubmit(k, &g_rng_req.work);
}
```

## Platform Integration

### x64 Platform

**Initialization sequence:**
1. Scan PCI bus for VirtIO devices
2. Parse PCI capabilities to find COMMON_CFG, NOTIFY_CFG, ISR_CFG BARs
3. Enable PCI bus mastering and memory access
4. Initialize device via common configuration
5. Setup virtqueue (allocate memory, write addresses)
6. Register LAPIC interrupt handler
7. Set DRIVER_OK status

**Interrupt handling:**
- Legacy PCI interrupt line
- ISR reads interrupt status from ISR_CFG BAR
- Sets `irq_pending` flag for deferred processing

**Platform hooks:**
- Cache operations: No-ops (hardware coherency)
- Memory barriers: `mfence`
- IRQ registration: LAPIC

**Files:** `platform/x64/platform_virtio.c`, `platform/x64/pci.c`

### arm64 Platform

**Initialization sequence:**
1. Scan memory regions for MMIO device magic value
2. Detect MMIO version (v1 vs v2)
3. Reset device (write 0 to status)
4. Feature negotiation (read device features, write driver features)
5. Setup virtqueue:
   - v1: Write `QUEUE_PFN` (physical address >> 12)
   - v2: Write separate descriptor/available/used addresses
6. Clean cache for virtqueue memory
7. Register GIC interrupt handler (edge-triggered SPI)
8. Set DRIVER_OK status

**Interrupt handling:**
- GIC SPI interrupts (IRQs 48-79)
- ISR reads `INTERRUPT_STATUS` register
- Sets `irq_pending` flag for deferred processing

**Cache coherency:**
- **Submission:** Clean cache for descriptor table and available ring
- **Completion:** Invalidate cache for used ring and data buffers
- Memory barriers (`dsb sy`) around register access

**Platform hooks:**
- Cache operations: `dc cvac`, `dc ivac`
- Memory barriers: `dsb sy`
- IRQ registration: GIC with edge-triggered configuration

**Files:** `platform/arm64/virtio_mmio.c`, `platform/arm64/interrupt.c`

## Device Initialization Protocol

Standard VirtIO initialization sequence (same for all transports):

1. **Reset device** - Write 0 to status register
2. **Acknowledge** - Set `VIRTIO_STATUS_ACKNOWLEDGE` bit
3. **Driver loaded** - Set `VIRTIO_STATUS_DRIVER` bit
4. **Feature negotiation:**
   - Read device features
   - Write accepted driver features
5. **Features OK** - Set `VIRTIO_STATUS_FEATURES_OK` bit
6. **Verify** - Re-read status, check `FEATURES_OK` still set
7. **Setup virtqueues:**
   - Select queue
   - Query queue size
   - Allocate memory (aligned to page boundary)
   - Write queue addresses to device
   - Enable queue (v2 only)
8. **Driver OK** - Set `VIRTIO_STATUS_DRIVER_OK` bit
9. **Enable interrupts** - Register and enable interrupt handler

If any step fails, write `VIRTIO_STATUS_FAILED` to status register.

## Implementation Status

### Working

✅ **x64 PCI VirtIO-RNG** - Fully functional
- Device discovery via PCI scan
- Request submission and completion
- Interrupt delivery and processing
- Integration with async work queue

✅ **arm64 MMIO device discovery** - Device detection working
✅ **arm64 VirtIO-RNG initialization** - Device initializes successfully
✅ **arm64 GIC interrupt configuration** - Interrupts properly configured

### In Progress

⚠️ **arm64 MMIO VirtIO-RNG** - Initialization works, debugging interrupt delivery
- Device accepts requests
- Interrupt handler registered
- Investigating: Device processing and interrupt generation

### Future Work

- Complete arm64 interrupt debugging
- Add VirtIO-Block support (Device ID 2)
- Add VirtIO-Net support (Device ID 1)
- Implement MSI-X for PCI transport
- Device tree parsing for dynamic MMIO device discovery
- Support for VirtIO MMIO v2 optimizations (packed queues)

## Design Rationale

### Deferred Interrupt Processing

**Pattern:** ISR sets `irq_pending` flag, `platform_tick()` processes completions before dispatching callbacks.

**Benefits:**
- Prevents reentrancy issues
- Simplifies locking requirements
- Processes all completions in batch
- Callbacks execute in predictable context

### Constant-Time Request Lookup

**Design:** `active_requests[desc_idx]` array for O(1) lookup.

**Benefits:**
- Fast completion processing in ISR
- Simple implementation
- No search or hash table overhead
- Array size equals virtqueue size (bounded)

### Platform Hook Pattern

**Design:** Transport code calls platform-provided functions for cache operations, barriers, and IRQ setup.

**Benefits:**
- Transport implementations are architecture-agnostic
- Platform code handles architecture-specific quirks
- New platforms reuse existing transport code
- Clear separation of concerns

### Transport Abstraction

**Design:** Device drivers use generic transport interfaces, work with both MMIO and PCI.

**Benefits:**
- Device drivers written once, work on all platforms
- Adding new transport doesn't require rewriting device drivers
- Platform can support multiple transports (e.g., arm64 with PCIe)

## Testing

### x64 with QEMU

```bash
make run ARCH=x64
```

Add to QEMU command line (already in Makefile):
```
-device virtio-rng-pci
```

**Expected output:**
```
Found VirtIO-RNG at PCI 0:4.0
VirtIO-RNG initialized successfully
Random bytes (32): [hex values]
```

### arm64 with QEMU

```bash
make run ARCH=arm64
```

QEMU virt machine includes virtio-mmio devices by default at 0x0a000000.

**Current output:**
```
VirtIO MMIO device at 0x0a003e00, IRQ 79
Device ID: 4
Found VirtIO-RNG (MMIO) device
Queue size: 256
VirtIO-RNG (MMIO) initialized successfully
RNG request submitted
```

### Debugging Tools

**GIC configuration dump** (`platform/arm64/interrupt.c`):
```c
void irq_dump_config(uint32_t irq_num);
```
Prints enabled state, priority, target CPU, trigger type, GIC registers.

**VirtIO register inspection:**
- Read `INTERRUPT_STATUS` register for pending interrupts
- Check used ring `idx` for device updates
- Verify descriptor table and ring memory contents

## Adding New Devices

To add a new VirtIO device (e.g., virtio-blk):

1. **Create device driver** (`driver/virtio/virtio_blk.c`):
   - Define device-specific request structure
   - Implement work submission logic
   - Implement completion processing
   - Use transport-agnostic virtqueue operations

2. **Add device ID** (`driver/virtio/virtio.h`):
   ```c
   #define VIRTIO_ID_BLOCK    2
   ```

3. **Update platform discovery** (`platform/*/platform_virtio.c`):
   - Check for device ID in discovery loop
   - Initialize device with appropriate transport
   - Register interrupt handler

4. **Add work queue operations** (`src/kapi.h`):
   ```c
   #define KWORK_OP_BLOCK_READ   3
   #define KWORK_OP_BLOCK_WRITE  4
   ```

5. **Implement device-specific logic:**
   - Request queuing
   - Multi-queue support if needed
   - Device-specific feature negotiation

The transport layer and platform integration remain unchanged.

## References

- [VirtIO Specification 1.2](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)

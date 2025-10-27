# VirtIO Device Integration

## Overview

This document describes how VirtIO devices integrate with vmos's async work queue architecture. We use **virtio-rng** (random number generator) as the first and primary example.

## Architecture

### Code Organization

```
src/virtio/
├── virtio.h          # VirtIO core structures (virtqueue, descriptors)
├── virtio.c          # VirtIO helper functions (virtqueue management)
├── virtio_rng.h      # RNG device API
└── virtio_rng.c      # RNG device driver

platform/x64/
├── pci.h/c           # PCI configuration space access
├── pci_scan.c        # PCI bus enumeration
├── virtio_pci.h/c    # VirtIO PCI-specific (BAR mapping, IRQ setup)
└── platform_init.c   # Device discovery and initialization
```

### Division of Responsibility

- **src/virtio/**: Platform-agnostic VirtIO protocol (virtqueue, descriptors, device logic)
- **platform/*/**: Platform-specific transport (PCI/MMIO access, IRQ routing, device discovery)

## VirtIO Core Structures

### Virtqueue

VirtIO uses virtqueues for device communication. Each virtqueue has three rings:

```c
// src/virtio/virtio.h

#define VIRTQ_DESC_F_NEXT     1   // Descriptor has next
#define VIRTQ_DESC_F_WRITE    2   // Device writes (vs read)
#define VIRTQ_DESC_F_INDIRECT 4   // Buffer contains descriptor list

// Virtqueue descriptor
typedef struct {
    uint64_t addr;     // Physical address
    uint32_t len;      // Length
    uint16_t flags;    // VIRTQ_DESC_F_*
    uint16_t next;     // Next descriptor (if NEXT flag set)
} virtq_desc_t;

// Available ring entry
typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];   // Variable size
} virtq_avail_t;

// Used ring entry
typedef struct {
    uint32_t id;       // Descriptor chain head
    uint32_t len;      // Bytes written
} virtq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[];  // Variable size
} virtq_used_t;

// Virtqueue management structure
typedef struct {
    uint16_t queue_size;
    uint16_t num_free;
    uint16_t free_head;          // Free list head
    uint16_t last_used_idx;      // Last processed used index

    virtq_desc_t* desc;          // Descriptor table
    virtq_avail_t* avail;        // Available ring
    virtq_used_t* used;          // Used ring

    // PCI notify offset (platform-specific)
    uint16_t notify_offset;
} virtqueue_t;
```

### Virtqueue Operations

```c
// Initialize virtqueue
void virtqueue_init(virtqueue_t* vq, uint16_t queue_size, void* base);

// Allocate a descriptor (returns index or VIRTQUEUE_NO_DESC if full)
#define VIRTQUEUE_NO_DESC 0xFFFF
uint16_t virtqueue_alloc_desc(virtqueue_t* vq);

// Setup descriptor
void virtqueue_add_desc(virtqueue_t* vq, uint16_t idx,
                        uint64_t addr, uint32_t len, uint16_t flags);

// Add descriptor chain to available ring
void virtqueue_add_avail(virtqueue_t* vq, uint16_t desc_idx);

// Kick device (notify new descriptors available)
void virtqueue_kick(virtqueue_t* vq);

// Check if used ring has entries
int virtqueue_has_used(virtqueue_t* vq);

// Get used descriptor (returns descriptor index and length)
void virtqueue_get_used(virtqueue_t* vq, uint16_t* desc_idx, uint32_t* len);

// Free descriptor
void virtqueue_free_desc(virtqueue_t* vq, uint16_t desc_idx);
```

## VirtIO-RNG Device

### Device Structure

```c
// platform/x64/virtio_pci.c (stored in platform_t)

typedef struct {
    // VirtIO device info
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_func;

    // BARs
    uint64_t common_cfg_bar;     // Common config BAR
    uint64_t notify_bar;         // Notification BAR
    uint64_t isr_bar;            // ISR status BAR

    // Virtqueue
    virtqueue_t vq;
    uint16_t queue_size;

    // IRQ routing
    uint8_t irq_vector;

    // Request tracking (constant-time lookup)
    krng_req_t* active_requests[256];  // Size = queue_size

    // Back-pointer to kernel
    kernel_t* kernel;
} virtio_rng_t;
```

### Request Tracking

The `active_requests` array provides **constant-time** lookup from descriptor index to work item:

- **Size**: Same as virtqueue size (typically 256)
- **Index**: Descriptor index from used ring
- **Value**: Pointer to `krng_req_t` for that descriptor

### Work Item Structure

```c
// src/virtio/virtio_rng.h

typedef struct {
    work_t work;         // Embedded work item
    uint8_t* buffer;     // Buffer to fill
    size_t length;       // Bytes requested
    size_t completed;    // Bytes actually read
    uint16_t desc_idx;   // VirtIO descriptor index
} krng_req_t;
```

## Integration Flow

### Initialization (platform_init)

```c
// platform/x64/platform_init.c

void platform_init(platform_t* platform, void* fdt) {
    // ... existing initialization ...

    // 1. Scan PCI bus for VirtIO devices
    pci_scan_devices(platform);

    // 2. Find virtio-rng (vendor=0x1AF4, device=0x1005 or 0x1044)
    virtio_rng_t* rng = find_virtio_rng(platform);

    if (rng != NULL) {
        // 3. Setup device
        virtio_rng_init(platform, rng);

        // 4. Register interrupt handler
        irq_register(rng->irq_vector, virtio_rng_irq_handler, rng);

        printk("VirtIO-RNG initialized (IRQ %d, queue size %d)\n",
               rng->irq_vector, rng->queue_size);
    }
}
```

### Device Initialization

```c
// platform/x64/virtio_pci.c

void virtio_rng_init(platform_t* platform, virtio_rng_t* rng) {
    // 1. Reset device
    virtio_write_status(rng, 0);

    // 2. Acknowledge device
    virtio_write_status(rng, VIRTIO_STATUS_ACKNOWLEDGE);

    // 3. Driver ready
    virtio_write_status(rng, VIRTIO_STATUS_DRIVER);

    // 4. Read device features
    uint64_t features = virtio_read_features(rng);

    // 5. Negotiate features (none needed for basic RNG)
    virtio_write_features(rng, 0);

    // 6. Setup virtqueue
    rng->queue_size = virtio_read_queue_size(rng, 0);
    void* queue_mem = allocate_virtqueue_memory(rng->queue_size);
    virtqueue_init(&rng->vq, rng->queue_size, queue_mem);
    virtio_set_queue(rng, 0, &rng->vq);

    // 7. Driver OK
    virtio_write_status(rng, VIRTIO_STATUS_DRIVER_OK);

    // 8. Initialize request tracking
    for (int i = 0; i < rng->queue_size; i++) {
        rng->active_requests[i] = NULL;
    }

    // 9. Store in platform
    platform->virtio_rng = rng;
}
```

### Work Submission

```c
// platform/x64/virtio_pci.c

void platform_submit(platform_t* platform, kwork_t* submissions, kwork_t* cancellations) {
    virtio_rng_t* rng = platform->virtio_rng;
    kernel_t* k = platform->kernel;

    // Process cancellations (best-effort, usually too late for RNG)
    kwork_t* work = cancellations;
    while (work != NULL) {
        if (work->op == KWORK_OP_RNG_READ) {
            krng_req_t* req = CONTAINER_OF(work, krng_req_t, work);
            // RNG requests complete too quickly to cancel effectively
            // Let them complete normally
        }
        work = work->next;
    }

    // Process submissions
    work = submissions;
    int submitted = 0;

    while (work != NULL) {
        kwork_t* next = work->next;

        if (work->op == KWORK_OP_RNG_READ) {
            krng_req_t* req = CONTAINER_OF(work, krng_req_t, work);

            // Allocate descriptor
            uint16_t desc_idx = virtqueue_alloc_desc(&rng->vq);

            if (desc_idx == VIRTQUEUE_NO_DESC) {
                // Queue full - immediate failure with backpressure
                kplatform_complete_work(k, work, KERR_BUSY);
                work = next;
                continue;
            }

            // Setup descriptor (device-writable buffer)
            virtqueue_add_desc(&rng->vq, desc_idx, req->buffer,
                              req->length, VIRTQ_DESC_F_WRITE);

            // Add to available ring
            virtqueue_add_avail(&rng->vq, desc_idx);

            // Track request for completion (constant-time lookup)
            req->desc_idx = desc_idx;
            rng->active_requests[desc_idx] = req;

            // Mark as live
            work->state = KWORK_STATE_LIVE;
            submitted++;
        }

        work = next;
    }

    // Kick device once for all descriptors (bulk submission)
    if (submitted > 0) {
        virtqueue_kick(&rng->vq);
    }
}
```

### Interrupt Handler

```c
// platform/x64/virtio_pci.c

static void virtio_rng_irq_handler(void* context) {
    virtio_rng_t* rng = (virtio_rng_t*)context;
    kernel_t* k = rng->kernel;

    // Acknowledge interrupt
    uint8_t isr_status = virtio_read_isr(rng);
    if (!(isr_status & 0x1)) {
        return;  // Not our interrupt
    }

    // Process all used descriptors
    while (virtqueue_has_used(&rng->vq)) {
        uint16_t desc_idx;
        uint32_t len;

        virtqueue_get_used(&rng->vq, &desc_idx, &len);

        // Constant-time lookup
        krng_req_t* req = rng->active_requests[desc_idx];

        if (req != NULL) {
            // Update completion count
            req->completed = len;

            // Mark work as complete (moves to ready queue)
            kplatform_complete_work(k, &req->work, KERR_OK);

            // Clear tracking
            rng->active_requests[desc_idx] = NULL;
        }

        // Free descriptor
        virtqueue_free_desc(&rng->vq, desc_idx);
    }

    // Update interrupt reason (for platform_wfi)
    platform_set_last_interrupt(&k->platform, PLATFORM_INT_RNG);
}
```

### User Code Example

```c
// User-space example

static krng_req_t g_rng_req;
static uint8_t g_random_buf[32];

static void on_random_ready(kwork_t* work) {
    krng_req_t* req = CONTAINER_OF(work, krng_req_t, work);

    if (work->result != KERR_OK) {
        printk("RNG failed: ");
        printk_dec(work->result);
        printk("\n");
        return;
    }

    printk("Random bytes (");
    printk_dec(req->completed);
    printk("): ");

    for (size_t i = 0; i < req->completed; i++) {
        printk_hex8(req->buffer[i]);
        if (i < req->completed - 1) printk(" ");
    }
    printk("\n");
}

void kusermain(kernel_t* k) {
    // Setup RNG request
    g_rng_req.work.op = KWORK_OP_RNG_READ;
    g_rng_req.work.callback = on_random_ready;
    g_rng_req.work.ctx = NULL;
    g_rng_req.work.state = KWORK_STATE_DEAD;
    g_rng_req.work.flags = 0;
    g_rng_req.buffer = g_random_buf;
    g_rng_req.length = 32;

    // Submit work
    kerr_t err = ksubmit(k, &g_rng_req.work);
    if (err != KERR_OK) {
        printk("ksubmit failed: ");
        printk_dec(err);
        printk("\n");
    }
}
```

## PCI Discovery

### PCI Configuration Space

```c
// platform/x64/pci.h

// PCI config space registers
#define PCI_REG_VENDOR_ID      0x00
#define PCI_REG_DEVICE_ID      0x02
#define PCI_REG_COMMAND        0x04
#define PCI_REG_STATUS         0x06
#define PCI_REG_CLASS_CODE     0x08
#define PCI_REG_HEADER_TYPE    0x0E
#define PCI_REG_BAR0           0x10

// PCI command register bits
#define PCI_CMD_IO_ENABLE      (1 << 0)
#define PCI_CMD_MEM_ENABLE     (1 << 1)
#define PCI_CMD_BUS_MASTER     (1 << 2)
#define PCI_CMD_INT_DISABLE    (1 << 10)

// VirtIO PCI vendor/device IDs
#define VIRTIO_PCI_VENDOR_ID   0x1AF4
#define VIRTIO_PCI_DEVICE_RNG_LEGACY  0x1005
#define VIRTIO_PCI_DEVICE_RNG_MODERN  0x1044

// Read/write PCI config space
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
```

### Device Scanning

```c
// platform/x64/pci_scan.c

void pci_scan_devices(platform_t* platform) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t vendor_device = pci_config_read32(bus, slot, 0, PCI_REG_VENDOR_ID);

            if (vendor_device == 0xFFFFFFFF) {
                continue;  // No device
            }

            uint16_t vendor = vendor_device & 0xFFFF;
            uint16_t device = vendor_device >> 16;

            if (vendor == VIRTIO_PCI_VENDOR_ID &&
                (device == VIRTIO_PCI_DEVICE_RNG_LEGACY ||
                 device == VIRTIO_PCI_DEVICE_RNG_MODERN)) {

                printk("Found VirtIO-RNG at PCI ");
                printk_dec(bus);
                printk(":");
                printk_dec(slot);
                printk(".0\n");

                setup_virtio_rng_device(platform, bus, slot, 0);
            }
        }
    }
}
```

### VirtIO PCI Capabilities

Modern VirtIO devices use PCI capabilities to expose BARs:

```c
// VirtIO PCI capability types
#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4

typedef struct {
    uint8_t cap_vndr;    // Generic PCI field: 0x09 = vendor-specific
    uint8_t cap_next;    // Next capability offset
    uint8_t cap_len;     // Capability length
    uint8_t cfg_type;    // VIRTIO_PCI_CAP_*
    uint8_t bar;         // BAR index
    uint8_t padding[3];
    uint32_t offset;     // Offset within BAR
    uint32_t length;     // Length of structure
} virtio_pci_cap_t;
```

## Backpressure Handling

When the virtqueue is full:

```c
uint16_t desc_idx = virtqueue_alloc_desc(&rng->vq);
if (desc_idx == VIRTQUEUE_NO_DESC) {
    // Immediate completion with KERR_BUSY
    kplatform_complete_work(k, work, KERR_BUSY);
    return;
}
```

The callback receives `work->result == KERR_BUSY` and can retry:

```c
void on_random_ready(work_t* work) {
    if (work->result == KERR_BUSY) {
        // Retry submission
        ksubmit(kernel, work);
        return;
    }

    // Normal processing
    // ...
}
```

## Cancellation

VirtIO-RNG cancellation is **best-effort** and handled within `platform_submit()`:

Cancellations are passed via the `cancellations` parameter. For VirtIO-RNG:

```c
// Inside platform_submit()
kwork_t* work = cancellations;
while (work != NULL) {
    if (work->op == KWORK_OP_RNG_READ) {
        krng_req_t* req = CONTAINER_OF(work, krng_req_t, work);

        // Check if still in flight
        if (rng->active_requests[req->desc_idx] == req) {
            // Can't cancel - device may already be writing
            // Let it complete normally
        }
    }
    work = work->next;
}
```

In practice, VirtIO-RNG requests complete so quickly that cancellation rarely succeeds.

## Future VirtIO Devices

This architecture extends to other VirtIO devices:

- **virtio-blk**: Block device (WORK_OP_BLOCK_READ, WORK_OP_BLOCK_WRITE)
- **virtio-net**: Network device (WORK_OP_NET_SEND, WORK_OP_NET_RECV)
- **virtio-console**: Serial console

Each device follows the same pattern:
1. PCI discovery and initialization
2. IRQ handler with constant-time request lookup
3. Work submission via `platform_submit_work()`
4. Completion via `kplatform_complete_work()`

## QEMU Configuration

Add to `Makefile` run rule:

```makefile
-device virtio-rng-pci
```

This creates a VirtIO-RNG PCI device on the virtual bus.

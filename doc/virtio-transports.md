# VirtIO Transport Abstraction

## Overview

This document analyzes the current virtio-mmio (arm64) and virtio-pci (x64) implementations and proposes moving transport implementations to `src/virtio/` as reusable components. This will allow any platform to use MMIO or PCI transports without code duplication.

**Goal**: If rv64 wants to use virtio-mmio devices, it should use the same `src/virtio/virtio_mmio.c` that arm64 uses, only providing platform-specific hooks (cache ops, memory barriers). Similarly, if arm64 gets PCIe support, it should reuse `src/virtio/virtio_pci.c` from x64.

## Current Architecture

### Code Location Problem

Currently, transport implementations are embedded in platform directories:

```
platform/arm64/virtio_mmio.c     (411 lines)
  - MMIO register access (generic to MMIO spec)
  - Device initialization (generic to VirtIO spec)
  - ARM64 cache ops (platform-specific)
  - RNG driver logic (device-specific)

platform/x64/virtio_pci.c        (335 lines)
  - PCI capability parsing (generic to PCI spec)
  - Device initialization (generic to VirtIO spec)
  - x64 memory barriers (platform-specific)
  - RNG driver logic (device-specific)
```

**Problem**: If rv64 wants to use virtio-mmio, it would have to copy most of arm64's virtio_mmio.c, even though the MMIO register layout and initialization sequence are identical across platforms.

### What's Generic vs Platform-Specific

#### Generic to MMIO Transport (same on arm64, rv64, any MMIO platform)
- ✅ MMIO register offsets and layout
- ✅ Device initialization sequence (reset → acknowledge → driver → features → driver_ok)
- ✅ Feature negotiation protocol
- ✅ Queue setup register writes
- ✅ Version 1 (legacy) vs version 2 (modern) differences
- ✅ Notification via QUEUE_NOTIFY register
- ✅ ISR status reading from INTERRUPT_STATUS register

#### Generic to PCI Transport (same on x64, arm64 with PCIe, rv64 with PCIe)
- ✅ PCI configuration space access protocol
- ✅ PCI capability parsing and structure layout
- ✅ VirtIO PCI capability types (COMMON_CFG, NOTIFY_CFG, ISR_CFG)
- ✅ BAR mapping and offset calculations
- ✅ Notify offset multiplier calculation
- ✅ Device initialization via common_cfg structure

#### Platform-Specific (varies by CPU architecture)
- ❌ Cache coherency operations (ARM64: dc cvac/ivac, x64: none, rv64: varies)
- ❌ Memory barrier instructions (ARM64: dsb, x64: mfence, rv64: fence)
- ❌ Physical to virtual address mapping (if MMU enabled)
- ❌ Interrupt controller integration (GIC vs LAPIC vs PLIC)
- ❌ Device discovery address ranges (MMIO: platform firmware determines base addresses)

#### Device-Specific (same for RNG on any transport)
- ✅ Work submission logic (descriptor allocation, buffer setup)
- ✅ Completion processing (used ring processing, callback dispatch)
- ✅ Request tracking (active_requests array)

## Current Code Duplication

### Between Platforms (MMIO example)

If rv64 implements virtio-mmio today, it would duplicate from arm64:

| Code | Generic? | Lines | Duplication Risk |
|------|----------|-------|------------------|
| MMIO register offset definitions | Yes | ~40 | Would copy to rv64 |
| Device initialization sequence | Yes | ~130 | Would copy to rv64 |
| Version 1 vs 2 handling | Yes | ~45 | Would copy to rv64 |
| Cache operations | No | ~35 | Platform-specific (good) |
| RNG driver logic | Device | ~180 | Would copy to rv64 |

**~80% of virtio_mmio.c would be duplicated to rv64**

### Between Devices (within same platform)

When adding virtio-blk to arm64:

| Code | Generic? | Duplication Risk |
|------|----------|------------------|
| MMIO register access | Transport | Would reference same MMIO code (if in src/virtio/) |
| Device initialization | Transport | Would duplicate if embedded in device driver |
| Block-specific logic | Device | New code (good) |
| Cache operations | Platform | Would reuse platform hooks (if designed well) |

## Proposed Architecture

### Three-Layer Design

```
┌─────────────────────────────────────────────────────────┐
│           Device Drivers (src/virtio/)                  │
│                                                          │
│  virtio_rng.c    virtio_blk.c    virtio_net.c          │
│  - Uses transport APIs                                  │
│  - Device-specific logic (RNG, block, network)         │
│  - Work submission/completion                           │
└────────────────────┬────────────────────────────────────┘
                     │ Calls
         ┌───────────┴────────────┐
         ▼                        ▼
┌────────────────────┐  ┌───────────────────┐
│  MMIO Transport    │  │  PCI Transport    │
│  (src/virtio/)     │  │  (src/virtio/)    │
│                    │  │                   │
│ virtio_mmio.c      │  │ virtio_pci.c      │
│ - Register access  │  │ - Cap parsing     │
│ - Init sequence    │  │ - BAR mapping     │
│ - V1/V2 handling   │  │ - Common cfg      │
└─────────┬──────────┘  └─────────┬─────────┘
          │                       │
          │ Uses platform hooks   │
          └───────────┬───────────┘
                      ▼
┌─────────────────────────────────────────────────────────┐
│         Platform Hooks (platform/*/platform_impl.h)     │
│                                                          │
│  arm64: dc cvac/ivac, dsb, GIC IRQ                     │
│  x64:   no-op cache, mfence, LAPIC IRQ                 │
│  rv64:  fence, fence.i, PLIC IRQ                       │
└─────────────────────────────────────────────────────────┘
```

### File Structure

```
src/virtio/
  virtio.h, virtio.c              # Virtqueue management (already exists)
  virtio_mmio.h, virtio_mmio.c    # MMIO transport (moved from platform/arm64/)
  virtio_pci.h, virtio_pci.c      # PCI transport (moved from platform/x64/)
  virtio_rng.h, virtio_rng.c      # RNG device driver (new, generic)
  virtio_blk.h, virtio_blk.c      # Block driver (future)

platform/arm64/
  platform_impl.h                 # Platform hooks (cache ops, barriers, IRQ)
  platform_virtio.c               # Device discovery, platform integration

platform/x64/
  platform_impl.h                 # Platform hooks (no-op cache, mfence, IRQ)
  platform_virtio.c               # Device discovery, platform integration
  pci.h, pci.c                    # Low-level PCI config access (x64-specific)

platform/rv64/                    # Future platform
  platform_impl.h                 # RISC-V hooks (fence ops, PLIC)
  platform_virtio.c               # Can use src/virtio/virtio_mmio.c!
```

## API Design

### Platform Hooks

Platforms provide hooks for architecture-specific operations:

```c
// platform/*/platform_impl.h

typedef struct {
    // Memory synchronization (cache coherency)
    void (*cache_clean)(void* addr, size_t size);      // Flush to RAM (before device reads)
    void (*cache_invalidate)(void* addr, size_size);   // Discard cache (before CPU reads)

    // Memory barriers
    void (*memory_barrier)(void);                       // Full barrier

    // Interrupt registration
    int (*irq_register)(uint32_t irq_num, void (*handler)(void*), void* context);
    void (*irq_enable)(uint32_t irq_num);
} platform_hooks_t;

// Each platform implements this
extern const platform_hooks_t* platform_get_hooks(void);
```

#### ARM64 Implementation

```c
// platform/arm64/platform_hooks.c

static void arm64_cache_clean(void* addr, size_t size) {
    uintptr_t start = (uintptr_t)addr & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = (uintptr_t)addr + size;

    for (uintptr_t va = start; va < end; va += CACHE_LINE_SIZE) {
        __asm__ volatile("dc cvac, %0" :: "r"(va) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

static void arm64_cache_invalidate(void* addr, size_t size) {
    uintptr_t start = (uintptr_t)addr & ~(CACHE_LINE_SIZE - 1);
    uintptr_t end = (uintptr_t)addr + size;

    for (uintptr_t va = start; va < end; va += CACHE_LINE_SIZE) {
        __asm__ volatile("dc ivac, %0" :: "r"(va) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

static void arm64_memory_barrier(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

static const platform_hooks_t arm64_hooks = {
    .cache_clean = arm64_cache_clean,
    .cache_invalidate = arm64_cache_invalidate,
    .memory_barrier = arm64_memory_barrier,
    .irq_register = gic_irq_register,
    .irq_enable = gic_irq_enable,
};

const platform_hooks_t* platform_get_hooks(void) {
    return &arm64_hooks;
}
```

#### x64 Implementation

```c
// platform/x64/platform_hooks.c

// x86-64 has hardware cache coherency for DMA
static void x64_cache_clean(void* addr, size_t size) {
    // No-op: x86-64 maintains cache coherency automatically
    (void)addr; (void)size;
}

static void x64_cache_invalidate(void* addr, size_t size) {
    // No-op: x86-64 maintains cache coherency automatically
    (void)addr; (void)size;
}

static void x64_memory_barrier(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static const platform_hooks_t x64_hooks = {
    .cache_clean = x64_cache_clean,
    .cache_invalidate = x64_cache_invalidate,
    .memory_barrier = x64_memory_barrier,
    .irq_register = lapic_irq_register,
    .irq_enable = lapic_irq_enable,
};

const platform_hooks_t* platform_get_hooks(void) {
    return &x64_hooks;
}
```

### Generic MMIO Transport

The MMIO transport moves to `src/virtio/` and uses platform hooks:

```c
// src/virtio/virtio_mmio.h

#define VIRTIO_MMIO_MAGIC_VALUE       0x000
#define VIRTIO_MMIO_VERSION           0x004
#define VIRTIO_MMIO_DEVICE_ID         0x008
// ... all register offsets (same on all platforms)

typedef struct {
    volatile uint8_t* base;          // MMIO base address
    uint32_t version;                // 1 = legacy, 2 = modern
    const platform_hooks_t* hooks;   // Platform callbacks
} virtio_mmio_transport_t;

// Initialize MMIO transport
int virtio_mmio_init(virtio_mmio_transport_t* mmio,
                     void* base_addr,
                     const platform_hooks_t* hooks);

// Reset device
void virtio_mmio_reset(virtio_mmio_transport_t* mmio);

// Set device status
void virtio_mmio_set_status(virtio_mmio_transport_t* mmio, uint8_t status);

// Get device status
uint8_t virtio_mmio_get_status(virtio_mmio_transport_t* mmio);

// Read device features
uint32_t virtio_mmio_get_features(virtio_mmio_transport_t* mmio, uint32_t select);

// Write driver features
void virtio_mmio_set_features(virtio_mmio_transport_t* mmio, uint32_t select,
                              uint32_t features);

// Setup queue
int virtio_mmio_setup_queue(virtio_mmio_transport_t* mmio,
                            uint16_t queue_idx,
                            virtqueue_t* vq,
                            uint16_t queue_size);

// Notify queue (kick device)
void virtio_mmio_notify_queue(virtio_mmio_transport_t* mmio, uint16_t queue_idx);

// Read ISR status
uint32_t virtio_mmio_read_isr(virtio_mmio_transport_t* mmio);
```

```c
// src/virtio/virtio_mmio.c (generic implementation)

static inline uint32_t mmio_read32(volatile uint8_t* base, uint32_t offset,
                                   const platform_hooks_t* hooks) {
    volatile uint32_t* addr = (volatile uint32_t*)(base + offset);
    hooks->memory_barrier();
    return *addr;
}

static inline void mmio_write32(volatile uint8_t* base, uint32_t offset,
                                uint32_t value, const platform_hooks_t* hooks) {
    volatile uint32_t* addr = (volatile uint32_t*)(base + offset);
    *addr = value;
    hooks->memory_barrier();
}

int virtio_mmio_init(virtio_mmio_transport_t* mmio,
                     void* base_addr,
                     const platform_hooks_t* hooks) {
    mmio->base = (volatile uint8_t*)base_addr;
    mmio->hooks = hooks;

    // Verify magic
    uint32_t magic = mmio_read32(mmio->base, VIRTIO_MMIO_MAGIC_VALUE, hooks);
    if (magic != VIRTIO_MMIO_MAGIC) {
        return -1;
    }

    // Read version
    mmio->version = mmio_read32(mmio->base, VIRTIO_MMIO_VERSION, hooks);
    if (mmio->version < 1 || mmio->version > 2) {
        return -1;
    }

    return 0;
}

void virtio_mmio_reset(virtio_mmio_transport_t* mmio) {
    mmio_write32(mmio->base, VIRTIO_MMIO_STATUS, 0, mmio->hooks);
}

void virtio_mmio_set_status(virtio_mmio_transport_t* mmio, uint8_t status) {
    mmio_write32(mmio->base, VIRTIO_MMIO_STATUS, status, mmio->hooks);
}

int virtio_mmio_setup_queue(virtio_mmio_transport_t* mmio,
                            uint16_t queue_idx,
                            virtqueue_t* vq,
                            uint16_t queue_size) {
    mmio_write32(mmio->base, VIRTIO_MMIO_QUEUE_SEL, queue_idx, mmio->hooks);
    mmio_write32(mmio->base, VIRTIO_MMIO_QUEUE_NUM, queue_size, mmio->hooks);

    if (mmio->version == 1) {
        // Legacy: use QUEUE_PFN
        uint64_t queue_addr = (uint64_t)vq;
        uint32_t pfn = (uint32_t)(queue_addr >> 12);
        mmio_write32(mmio->base, VIRTIO_MMIO_QUEUE_ALIGN, 4096, mmio->hooks);
        mmio_write32(mmio->base, VIRTIO_MMIO_QUEUE_PFN, pfn, mmio->hooks);
    } else {
        // Modern: separate address registers
        uint64_t desc_addr = (uint64_t)vq->desc;
        mmio_write32(mmio->base, VIRTIO_MMIO_QUEUE_DESC_LOW,
                    (uint32_t)desc_addr, mmio->hooks);
        mmio_write32(mmio->base, VIRTIO_MMIO_QUEUE_DESC_HIGH,
                    (uint32_t)(desc_addr >> 32), mmio->hooks);

        uint64_t avail_addr = (uint64_t)vq->avail;
        mmio_write32(mmio->base, VIRTIO_MMIO_QUEUE_DRIVER_LOW,
                    (uint32_t)avail_addr, mmio->hooks);
        mmio_write32(mmio->base, VIRTIO_MMIO_QUEUE_DRIVER_HIGH,
                    (uint32_t)(avail_addr >> 32), mmio->hooks);

        uint64_t used_addr = (uint64_t)vq->used;
        mmio_write32(mmio->base, VIRTIO_MMIO_QUEUE_DEVICE_LOW,
                    (uint32_t)used_addr, mmio->hooks);
        mmio_write32(mmio->base, VIRTIO_MMIO_QUEUE_DEVICE_HIGH,
                    (uint32_t)(used_addr >> 32), mmio->hooks);

        mmio_write32(mmio->base, VIRTIO_MMIO_QUEUE_READY, 1, mmio->hooks);
    }

    // Clean cache so device can see initialized queue
    size_t queue_mem_size = 64 * 1024;  // Conservative size
    mmio->hooks->cache_clean(vq, queue_mem_size);

    return 0;
}

void virtio_mmio_notify_queue(virtio_mmio_transport_t* mmio, uint16_t queue_idx) {
    mmio_write32(mmio->base, VIRTIO_MMIO_QUEUE_NOTIFY, queue_idx, mmio->hooks);
}
```

### Generic PCI Transport

Similarly, PCI transport moves to `src/virtio/`:

```c
// src/virtio/virtio_pci.h

typedef struct {
    // PCI location
    uint8_t bus, slot, func;

    // Capabilities
    volatile virtio_pci_common_cfg_t* common_cfg;
    volatile uint8_t* isr_status;
    uint64_t notify_base;
    uint32_t notify_off_multiplier;

    // Platform hooks
    const platform_hooks_t* hooks;
} virtio_pci_transport_t;

// Initialize PCI transport (parses capabilities)
int virtio_pci_init(virtio_pci_transport_t* pci,
                    uint8_t bus, uint8_t slot, uint8_t func,
                    const platform_hooks_t* hooks);

// Device control
void virtio_pci_reset(virtio_pci_transport_t* pci);
void virtio_pci_set_status(virtio_pci_transport_t* pci, uint8_t status);
uint8_t virtio_pci_get_status(virtio_pci_transport_t* pci);

// Feature negotiation
uint32_t virtio_pci_get_features(virtio_pci_transport_t* pci, uint32_t select);
void virtio_pci_set_features(virtio_pci_transport_t* pci, uint32_t select,
                             uint32_t features);

// Queue operations
int virtio_pci_setup_queue(virtio_pci_transport_t* pci,
                           uint16_t queue_idx,
                           virtqueue_t* vq,
                           uint16_t queue_size);
void virtio_pci_notify_queue(virtio_pci_transport_t* pci, uint16_t queue_idx);

// ISR
uint8_t virtio_pci_read_isr(virtio_pci_transport_t* pci);
```

**Note**: PCI config space access (`pci_config_read*`, `pci_read_bar`) remains platform-specific in `platform/x64/pci.c` because it involves I/O port access on x64, which is architecture-specific. The generic `virtio_pci.c` calls these platform functions.

### Generic Device Drivers

Device drivers use transport APIs, agnostic to MMIO vs PCI:

```c
// src/virtio/virtio_rng.h

typedef struct {
    // Transport (either MMIO or PCI)
    void* transport;                    // Points to virtio_mmio_transport_t or virtio_pci_transport_t
    int transport_type;                 // VIRTIO_TRANSPORT_MMIO or VIRTIO_TRANSPORT_PCI

    // Virtqueue
    virtqueue_t vq;
    void* vq_memory;
    uint16_t queue_size;

    // Request tracking
    krng_req_t* active_requests[256];
    volatile uint8_t irq_pending;

    // Platform
    const platform_hooks_t* hooks;
    kernel_t* kernel;
} virtio_rng_dev_t;

// Initialize RNG with MMIO transport
int virtio_rng_init_mmio(virtio_rng_dev_t* rng,
                         virtio_mmio_transport_t* mmio,
                         void* queue_memory,
                         kernel_t* kernel);

// Initialize RNG with PCI transport
int virtio_rng_init_pci(virtio_rng_dev_t* rng,
                        virtio_pci_transport_t* pci,
                        void* queue_memory,
                        kernel_t* kernel);

// Process interrupt (transport-agnostic)
void virtio_rng_process_irq(virtio_rng_dev_t* rng, kernel_t* k);

// Submit work (transport-agnostic)
void virtio_rng_submit_work(virtio_rng_dev_t* rng, kwork_t* submissions,
                            kernel_t* k);
```

```c
// src/virtio/virtio_rng.c

int virtio_rng_init_mmio(virtio_rng_dev_t* rng,
                         virtio_mmio_transport_t* mmio,
                         void* queue_memory,
                         kernel_t* kernel) {
    rng->transport = mmio;
    rng->transport_type = VIRTIO_TRANSPORT_MMIO;
    rng->hooks = mmio->hooks;
    rng->kernel = kernel;

    // Reset
    virtio_mmio_reset(mmio);

    // Acknowledge
    virtio_mmio_set_status(mmio, VIRTIO_STATUS_ACKNOWLEDGE);

    // Driver
    virtio_mmio_set_status(mmio, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    // Feature negotiation (RNG needs no features)
    virtio_mmio_set_features(mmio, 0, 0);

    // Features OK
    uint8_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                     VIRTIO_STATUS_FEATURES_OK;
    virtio_mmio_set_status(mmio, status);

    // Verify
    if (!(virtio_mmio_get_status(mmio) & VIRTIO_STATUS_FEATURES_OK)) {
        return -1;
    }

    // Setup queue
    rng->vq_memory = queue_memory;
    rng->queue_size = 256;
    virtqueue_init(&rng->vq, rng->queue_size, queue_memory);
    virtio_mmio_setup_queue(mmio, 0, &rng->vq, rng->queue_size);

    // Driver OK
    status |= VIRTIO_STATUS_DRIVER_OK;
    virtio_mmio_set_status(mmio, status);

    return 0;
}

void virtio_rng_submit_work(virtio_rng_dev_t* rng, kwork_t* submissions,
                            kernel_t* k) {
    int submitted = 0;

    kwork_t* work = submissions;
    while (work != NULL) {
        kwork_t* next = work->next;

        if (work->op == KWORK_OP_RNG_READ) {
            krng_req_t* req = CONTAINER_OF(work, krng_req_t, work);

            uint16_t desc_idx = virtqueue_alloc_desc(&rng->vq);
            if (desc_idx == VIRTQUEUE_NO_DESC) {
                kplatform_complete_work(k, work, KERR_BUSY);
                work = next;
                continue;
            }

            virtqueue_add_desc(&rng->vq, desc_idx, (uint64_t)req->buffer,
                              req->length, VIRTQ_DESC_F_WRITE);
            virtqueue_add_avail(&rng->vq, desc_idx);

            req->platform.desc_idx = desc_idx;
            rng->active_requests[desc_idx] = req;
            work->state = KWORK_STATE_LIVE;
            submitted++;
        }

        work = next;
    }

    if (submitted > 0) {
        // Clean cache (ARM64 will flush, x64 is no-op)
        size_t desc_size = rng->queue_size * sizeof(virtq_desc_t);
        size_t avail_size = 4 + rng->queue_size * 2 + 2;
        rng->hooks->cache_clean(rng->vq.desc, desc_size);
        rng->hooks->cache_clean(rng->vq.avail, avail_size);

        // Notify device (transport-specific implementation)
        if (rng->transport_type == VIRTIO_TRANSPORT_MMIO) {
            virtio_mmio_notify_queue((virtio_mmio_transport_t*)rng->transport, 0);
        } else {
            virtio_pci_notify_queue((virtio_pci_transport_t*)rng->transport, 0);
        }
    }
}

void virtio_rng_process_irq(virtio_rng_dev_t* rng, kernel_t* k) {
    if (!rng->irq_pending) {
        return;
    }
    rng->irq_pending = 0;

    // Invalidate used ring cache (ARM64 will invalidate, x64 is no-op)
    size_t used_size = 4 + rng->queue_size * sizeof(virtq_used_elem_t) + 2;
    rng->hooks->cache_invalidate(rng->vq.used, used_size);

    while (virtqueue_has_used(&rng->vq)) {
        uint16_t desc_idx;
        uint32_t len;
        virtqueue_get_used(&rng->vq, &desc_idx, &len);

        krng_req_t* req = rng->active_requests[desc_idx];
        if (req != NULL) {
            // Invalidate buffer cache
            rng->hooks->cache_invalidate(req->buffer, req->length);

            req->completed = len;
            kplatform_complete_work(k, &req->work, KERR_OK);
            rng->active_requests[desc_idx] = NULL;
        }

        virtqueue_free_desc(&rng->vq, desc_idx);
    }
}
```

### Platform Integration

Platforms only handle discovery and glue:

```c
// platform/arm64/platform_virtio.c

void platform_discover_virtio(platform_t* platform) {
    const platform_hooks_t* hooks = platform_get_hooks();

    // Scan MMIO addresses for VirtIO devices
    for (int slot = 0; slot < 32; slot++) {
        uint64_t base = 0x0a000000 + (slot * 0x200);

        virtio_mmio_transport_t mmio;
        if (virtio_mmio_init(&mmio, (void*)base, hooks) < 0) {
            continue;
        }

        uint32_t device_id = mmio_read32(mmio.base, VIRTIO_MMIO_DEVICE_ID, hooks);

        if (device_id == VIRTIO_ID_RNG) {
            // Setup RNG device
            virtio_rng_dev_t* rng = &platform->virtio_rng;
            virtio_rng_init_mmio(rng, &mmio, platform->virtqueue_memory,
                                platform->kernel);

            // Register interrupt
            uint32_t irq = 32 + slot;
            hooks->irq_register(irq, virtio_rng_irq_handler, rng);
            hooks->irq_enable(irq);

            platform->has_virtio_rng = 1;
        }
    }
}
```

```c
// platform/x64/platform_virtio.c

void platform_discover_virtio(platform_t* platform) {
    const platform_hooks_t* hooks = platform_get_hooks();

    // Scan PCI bus for VirtIO devices
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_config_read16(bus, slot, 0, PCI_REG_VENDOR_ID);
            uint16_t device = pci_config_read16(bus, slot, 0, PCI_REG_DEVICE_ID);

            if (vendor == 0x1AF4 && (device == 0x1005 || device == 0x1044)) {
                virtio_pci_transport_t pci;
                if (virtio_pci_init(&pci, bus, slot, 0, hooks) < 0) {
                    continue;
                }

                uint16_t device_id = pci.common_cfg->device_id;

                if (device_id == VIRTIO_ID_RNG) {
                    virtio_rng_dev_t* rng = kmalloc(sizeof(virtio_rng_dev_t));
                    virtio_rng_init_pci(rng, &pci, g_virtqueue_memory,
                                       platform->kernel);

                    uint8_t irq_line = pci_config_read8(bus, slot, 0,
                                                        PCI_REG_INTERRUPT_LINE);
                    hooks->irq_register(32 + irq_line, virtio_rng_irq_handler, rng);
                    hooks->irq_enable(32 + irq_line);

                    platform->virtio_rng = rng;
                }
            }
        }
    }
}
```

**Key point**: rv64 would create nearly identical `platform/rv64/platform_virtio.c`, using the same `src/virtio/virtio_mmio.c` code!

## Benefits

### Code Reuse Across Platforms

| Component | Before | After |
|-----------|--------|-------|
| MMIO register access | Duplicated per platform | Shared in src/virtio/ |
| PCI capability parsing | Duplicated per platform | Shared in src/virtio/ |
| Device initialization | Duplicated per platform | Shared in src/virtio/ |
| RNG driver logic | Duplicated per platform | Shared in src/virtio/ |
| Cache operations | Platform-specific (good) | Platform hooks (good) |

**Result**: rv64 can support virtio-mmio by writing ~50 lines of platform glue code instead of copying ~400 lines from arm64.

### Code Reuse Across Devices

When adding virtio-blk:

| Component | Code Location | Reused? |
|-----------|---------------|---------|
| MMIO transport | src/virtio/virtio_mmio.c | Yes ✅ |
| PCI transport | src/virtio/virtio_pci.c | Yes ✅ |
| Block driver | src/virtio/virtio_blk.c | New (expected) |
| Platform hooks | platform/*/platform_hooks.c | Yes ✅ |

### Clear Separation of Concerns

```
src/virtio/          → Generic VirtIO protocol code (MMIO, PCI, devices)
platform/*/          → Architecture-specific code (cache, barriers, IRQ)
```

No mixing of concerns. Each layer has clear responsibilities.

### Platform Can Support Both MMIO and PCI

Example: arm64 with PCIe support:

```c
// platform/arm64/platform_virtio.c

void platform_discover_virtio(platform_t* platform) {
    const platform_hooks_t* hooks = platform_get_hooks();

    // Scan MMIO slots
    scan_mmio_virtio_devices(platform, hooks);

    // If platform has PCIe, scan PCI bus too
    if (platform_has_pcie()) {
        scan_pci_virtio_devices(platform, hooks);
    }
}
```

Same platform can use both transports, sharing device drivers and platform hooks.

## Migration Path

### Phase 1: Extract Platform Hooks (No Functional Changes)

1. Create `platform_hooks_t` structure
2. Implement hooks for arm64 (cache ops, dsb)
3. Implement hooks for x64 (no-op cache, mfence)
4. Current code calls hooks instead of inline functions
5. Verify both platforms still work

### Phase 2: Move MMIO Transport to src/virtio/

1. Create `src/virtio/virtio_mmio.{h,c}`
2. Move MMIO register access from `platform/arm64/virtio_mmio.c`
3. Update MMIO code to use platform hooks
4. Update arm64 to use `src/virtio/virtio_mmio.c`
5. Test arm64 thoroughly

### Phase 3: Move PCI Transport to src/virtio/

1. Create `src/virtio/virtio_pci.{h,c}`
2. Move PCI capability parsing from `platform/x64/virtio_pci.c`
3. Update PCI code to use platform hooks
4. Keep low-level PCI config access in `platform/x64/pci.c`
5. Update x64 to use `src/virtio/virtio_pci.c`
6. Test x64 thoroughly

### Phase 4: Extract Generic RNG Driver

1. Create `src/virtio/virtio_rng.{h,c}`
2. Move device logic (submit, process_irq) from platform code
3. Update both platforms to use generic driver
4. Remove duplicated RNG code from platform directories
5. Test both platforms

### Phase 5: Validate with New Platform (Optional)

1. Add rv64 platform support
2. Implement rv64 platform hooks (fence instructions, PLIC)
3. Create `platform/rv64/platform_virtio.c` (~50 lines)
4. Should work with existing `src/virtio/virtio_mmio.c` and `src/virtio/virtio_rng.c`

## Open Questions

### 1. PCI Config Access Portability

**Issue**: x64 uses I/O port instructions (inl/outl) for PCI config access. ARM64 with PCIe uses MMIO-based config access.

**Options**:
- Keep PCI config access platform-specific (current proposal)
- Abstract PCI config access into platform hooks
- Create generic ECAM (Enhanced Configuration Access Mechanism) code

**Recommendation**: Start with platform-specific PCI code. If arm64 adds PCIe, revisit with ECAM abstraction.

### 2. Physical vs Virtual Addressing

**Issue**: Some platforms may enable MMU, requiring physical address translation.

**Options**:
- Assume identity mapping (phys == virt) for early boot
- Add virt_to_phys hook to platform_hooks_t
- Pass physical addresses from platform discovery

**Recommendation**: Start with identity mapping assumption. Add virt_to_phys hook when MMU support is added.

### 3. Device Tree Integration

**Issue**: arm64 should discover MMIO base addresses from device tree, not hardcode slots.

**Options**:
- Platform parses DT and passes addresses to generic code
- Generic MMIO code learns to parse DT (couples with DT implementation)

**Recommendation**: Platform responsibility. `platform_virtio.c` parses DT and calls `virtio_mmio_init()` with discovered addresses.

### 4. Multiple Devices of Same Type

**Issue**: What if platform has 2+ virtio-rng devices?

**Options**:
- Platform allocates array of device structures
- Use linked list of devices
- Limit to one device per type for simplicity

**Recommendation**: Start with one device per type. If multiple devices needed, platform can allocate array and register each.

### 5. MSI-X Support (PCI)

**Issue**: Current PCI code uses legacy interrupts. MSI-X is more efficient.

**Options**:
- Add MSI-X support to generic PCI transport
- Make MSI-X vs legacy a platform choice

**Recommendation**: Add to `virtio_pci.c` later. Platform hooks already abstract IRQ registration, so should be straightforward.

## Conclusion

Moving MMIO and PCI transports to `src/virtio/` as reusable components provides:

1. **Massive code reuse** - New platforms (rv64) don't duplicate MMIO/PCI code
2. **Clear layering** - Transport protocols separate from platform quirks
3. **Shared device drivers** - RNG/block/net drivers work on any platform/transport
4. **Maintainability** - Fix bugs once in generic code, benefits all platforms

The platform hook design keeps architecture-specific operations (cache, barriers, IRQ) in platform code where they belong, while eliminating duplication of protocol-level code (MMIO registers, PCI capabilities, device initialization).

**Key insight**: MMIO and PCI are *transport specifications*, not platform implementations. They should be treated as reusable libraries, not platform-specific code.

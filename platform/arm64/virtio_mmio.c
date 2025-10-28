// ARM64 VirtIO MMIO Transport Implementation
// VirtIO-RNG device driver using MMIO transport

#include "virtio_mmio.h"
#include "interrupt.h"
#include "platform_impl.h"
#include "printk.h"
#include <stddef.h>

// VirtIO queue size
#define VIRTQUEUE_SIZE 256

// Memory barrier for ARM64
// Use dsb (Data Synchronization Barrier) for device I/O to ensure
// all memory operations complete before device sees them
static inline void mfence(void) { __asm__ volatile("dsb sy" ::: "memory"); }

// ARM64 Cache Maintenance Operations
// These are critical for DMA coherency - the device reads/writes RAM directly,
// so we must ensure CPU cache is synchronized with RAM

#define CACHE_LINE_SIZE 64

// Clean (flush) cache for a memory range - pushes CPU writes to RAM
// Call this AFTER CPU writes data that the device will read
static void cache_clean_range(void *addr, size_t size) {
  uintptr_t start = (uintptr_t)addr;
  uintptr_t end = start + size;

  // Align down to cache line boundary
  start &= ~(CACHE_LINE_SIZE - 1);

  // Clean each cache line in the range
  for (uintptr_t va = start; va < end; va += CACHE_LINE_SIZE) {
    __asm__ volatile("dc cvac, %0" ::"r"(va) : "memory");
  }

  // Ensure all cache operations complete
  __asm__ volatile("dsb sy" ::: "memory");
}

// Invalidate cache for a memory range - discards stale CPU cache
// Call this BEFORE CPU reads data that the device has written
static void cache_invalidate_range(void *addr, size_t size) {
  uintptr_t start = (uintptr_t)addr;
  uintptr_t end = start + size;

  // Align down to cache line boundary
  start &= ~(CACHE_LINE_SIZE - 1);

  // Invalidate each cache line in the range
  for (uintptr_t va = start; va < end; va += CACHE_LINE_SIZE) {
    __asm__ volatile("dc ivac, %0" ::"r"(va) : "memory");
  }

  // Ensure all cache operations complete
  __asm__ volatile("dsb sy" ::: "memory");
}

// Helper: Write to MMIO register
static void mmio_write32(volatile uint8_t *base, uint32_t offset,
                         uint32_t value) {
  volatile void *ptr = (volatile void *)(base + offset);
  volatile uint32_t *addr = (volatile uint32_t *)ptr;
  *addr = value;
  mfence();
}

// Helper: Read from MMIO register
static uint32_t mmio_read32(volatile uint8_t *base, uint32_t offset) {
  volatile void *ptr = (volatile void *)(base + offset);
  volatile uint32_t *addr = (volatile uint32_t *)ptr;
  mfence();
  return *addr;
}

// VirtIO-RNG interrupt handler (minimal - deferred processing pattern)
static void virtio_rng_irq_handler(void *context) {
  virtio_rng_t *rng = (virtio_rng_t *)context;

  printk("! VirtIO-RNG IRQ !\n");

  // Read and acknowledge interrupt status
  // This clears the interrupt line according to VirtIO MMIO spec
  uint32_t isr_status =
      mmio_read32(rng->mmio_base, VIRTIO_MMIO_INTERRUPT_STATUS);
  printk("ISR status: 0x");
  printk_hex32(isr_status);
  printk("\n");
  mmio_write32(rng->mmio_base, VIRTIO_MMIO_INTERRUPT_ACK, isr_status);

  // Set pending flag for deferred processing in ktick
  rng->irq_pending = 1;
}

// Setup and initialize VirtIO-RNG device via MMIO
void virtio_rng_mmio_setup(platform_t *platform, uint64_t mmio_base,
                           uint64_t mmio_size, uint32_t irq_num) {
  virtio_rng_t *rng = &platform->virtio_rng;

  rng->mmio_base = (volatile uint8_t *)mmio_base;
  rng->mmio_size = mmio_size;
  rng->irq_num = irq_num;

  printk("VirtIO MMIO device at 0x");
  printk_hex64(mmio_base);
  printk(", IRQ ");
  printk_dec(irq_num);
  printk("\n");

  // Verify magic value
  uint32_t magic = mmio_read32(rng->mmio_base, VIRTIO_MMIO_MAGIC_VALUE);
  if (magic != VIRTIO_MMIO_MAGIC) {
    printk("Invalid VirtIO magic: 0x");
    printk_hex32(magic);
    printk("\n");
    return;
  }

  // Check version (1 = legacy, 2 = modern)
  uint32_t version = mmio_read32(rng->mmio_base, VIRTIO_MMIO_VERSION);
  printk("VirtIO MMIO version: ");
  printk_dec(version);
  printk("\n");

  if (version < 1 || version > 2) {
    printk("Unsupported VirtIO version\n");
    return;
  }

  // Check device ID (4 = RNG)
  printk("Reading device ID...\n");
  uint32_t device_id = mmio_read32(rng->mmio_base, VIRTIO_MMIO_DEVICE_ID);
  printk("Device ID: ");
  printk_dec(device_id);
  printk("\n");

  if (device_id == 0) {
    // Device ID 0 means no device at this address
    printk("No device at this address\n");
    return;
  }
  if (device_id != VIRTIO_ID_RNG) {
    printk("Not a VirtIO-RNG device (ID=");
    printk_dec(device_id);
    printk(")\n");
    return;
  }

  printk("Found VirtIO-RNG (MMIO) device\n");

  // Reset device
  printk("Resetting device...\n");
  mmio_write32(rng->mmio_base, VIRTIO_MMIO_STATUS, 0);

  // Acknowledge device
  printk("Acknowledging device...\n");
  mmio_write32(rng->mmio_base, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);

  // Driver ready
  printk("Setting driver status...\n");
  uint32_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
  mmio_write32(rng->mmio_base, VIRTIO_MMIO_STATUS, status);

  // Read device features
  printk("Reading features...\n");
  mmio_write32(rng->mmio_base, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
  uint32_t features = mmio_read32(rng->mmio_base, VIRTIO_MMIO_DEVICE_FEATURES);
  printk("Device features: 0x");
  printk_hex32(features);
  printk("\n");

  // Write driver features (none needed for basic RNG)
  printk("Setting driver features...\n");
  mmio_write32(rng->mmio_base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
  mmio_write32(rng->mmio_base, VIRTIO_MMIO_DRIVER_FEATURES, 0);

  // Features OK
  printk("Setting FEATURES_OK...\n");
  status |= VIRTIO_STATUS_FEATURES_OK;
  mmio_write32(rng->mmio_base, VIRTIO_MMIO_STATUS, status);

  // Verify features OK
  printk("Verifying FEATURES_OK...\n");
  status = mmio_read32(rng->mmio_base, VIRTIO_MMIO_STATUS);
  printk("Status after FEATURES_OK: 0x");
  printk_hex8(status);
  printk("\n");
  if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
    printk("Device rejected features\n");
    return;
  }

  // Setup virtqueue 0 (requestq)
  mmio_write32(rng->mmio_base, VIRTIO_MMIO_QUEUE_SEL, 0);
  uint32_t queue_num_max =
      mmio_read32(rng->mmio_base, VIRTIO_MMIO_QUEUE_NUM_MAX);

  rng->queue_size = queue_num_max;
  if (rng->queue_size > VIRTQUEUE_SIZE) {
    rng->queue_size = VIRTQUEUE_SIZE;
  }

  printk("Queue size: ");
  printk_dec(rng->queue_size);
  printk("\n");

  // Allocate and initialize virtqueue from platform memory
  rng->vq_memory = platform->virtqueue_memory;
  virtqueue_init(&rng->vq, rng->queue_size, rng->vq_memory);

  // Set queue size
  mmio_write32(rng->mmio_base, VIRTIO_MMIO_QUEUE_NUM, rng->queue_size);

  // Configure queue addresses based on version
  if (version == 1) {
    // Legacy (version 1): Use QUEUE_PFN with page alignment
    printk("Using legacy QUEUE_PFN setup (v1)\n");

    // Set alignment to 4096 (page size)
    mmio_write32(rng->mmio_base, VIRTIO_MMIO_QUEUE_ALIGN, 4096);

    // Set Page Frame Number (physical address >> 12)
    uint64_t queue_addr = (uint64_t)rng->vq_memory;
    uint32_t pfn = (uint32_t)(queue_addr >> 12);
    mmio_write32(rng->mmio_base, VIRTIO_MMIO_QUEUE_PFN, pfn);

    printk("Queue PFN: 0x");
    printk_hex32(pfn);
    printk(" (addr: 0x");
    printk_hex64(queue_addr);
    printk(")\n");
  } else {
    // Modern (version 2+): Use separate address registers
    printk("Using modern queue address setup (v2+)\n");

    uint64_t desc_addr = (uint64_t)rng->vq.desc;
    mmio_write32(rng->mmio_base, VIRTIO_MMIO_QUEUE_DESC_LOW,
                 (uint32_t)desc_addr);
    mmio_write32(rng->mmio_base, VIRTIO_MMIO_QUEUE_DESC_HIGH,
                 (uint32_t)(desc_addr >> 32));

    uint64_t avail_addr = (uint64_t)rng->vq.avail;
    mmio_write32(rng->mmio_base, VIRTIO_MMIO_QUEUE_DRIVER_LOW,
                 (uint32_t)avail_addr);
    mmio_write32(rng->mmio_base, VIRTIO_MMIO_QUEUE_DRIVER_HIGH,
                 (uint32_t)(avail_addr >> 32));

    uint64_t used_addr = (uint64_t)rng->vq.used;
    mmio_write32(rng->mmio_base, VIRTIO_MMIO_QUEUE_DEVICE_LOW,
                 (uint32_t)used_addr);
    mmio_write32(rng->mmio_base, VIRTIO_MMIO_QUEUE_DEVICE_HIGH,
                 (uint32_t)(used_addr >> 32));

    // Enable queue (version 2+ only)
    mmio_write32(rng->mmio_base, VIRTIO_MMIO_QUEUE_READY, 1);

    // Verify queue was enabled
    uint32_t queue_ready = mmio_read32(rng->mmio_base, VIRTIO_MMIO_QUEUE_READY);
    printk("Queue ready: ");
    printk_dec(queue_ready);
    printk("\n");
  }

  // Request tracking and irq_pending flag zeroed by BSS initialization
  rng->irq_pending = 0;

  // CRITICAL: Clean entire virtqueue memory to ensure device sees initialized
  // state This includes descriptor table, available ring, and used ring
  printk("Cleaning virtqueue cache...\n");
  cache_clean_range(rng->vq_memory, VIRTQUEUE_MEMORY_SIZE);

  // Register interrupt handler
  irq_register(rng->irq_num, virtio_rng_irq_handler, rng);

  // Driver OK
  status |= VIRTIO_STATUS_DRIVER_OK;
  mmio_write32(rng->mmio_base, VIRTIO_MMIO_STATUS, status);

  // Mark device as present
  platform->virtio_rng_present = 1;

  // Enable IRQ now that everything is set up
  irq_enable(rng->irq_num);

  // Dump GIC configuration for debugging
  irq_dump_config(rng->irq_num);

  printk("VirtIO-RNG (MMIO) initialized successfully\n");
}

// Process deferred interrupt work (called from ktick before callbacks)
void kplatform_tick(platform_t *platform, kernel_t *k) {
  if (!platform->virtio_rng_present) {
    return;
  }

  virtio_rng_t *rng = &platform->virtio_rng;

  // Deferred interrupt processing - only process if interrupt occurred
  if (!rng->irq_pending) {
    return;
  }

  rng->irq_pending = 0;

  // CRITICAL: Invalidate cache before reading device-written data
  cache_invalidate_range(&rng->vq.used->idx, sizeof(uint16_t));

  // Now invalidate the full used ring since we know there's work to process
  size_t used_size = 4 + rng->queue_size * sizeof(virtq_used_elem_t) + 2;
  cache_invalidate_range(rng->vq.used, used_size);

  // Process all used descriptors
  while (virtqueue_has_used(&rng->vq)) {
    uint16_t desc_idx;
    uint32_t len;

    virtqueue_get_used(&rng->vq, &desc_idx, &len);

    // Constant-time lookup
    krng_req_t *req = rng->active_requests[desc_idx];

    if (req != NULL) {
      // CRITICAL: Invalidate buffer cache - device wrote random data to this
      // buffer
      cache_invalidate_range(req->buffer, req->length);

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
}

// Platform submit function (called from ktick after callbacks)
void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations) {
  if (!platform->virtio_rng_present) {
    // No RNG device, complete all submissions with error
    kwork_t *work = submissions;
    while (work != NULL) {
      kwork_t *next = work->next;
      kplatform_complete_work(platform->kernel, work, KERR_NO_DEVICE);
      work = next;
    }
    return;
  }

  virtio_rng_t *rng = &platform->virtio_rng;
  void *k = platform->kernel;

  // Process cancellations (best-effort, usually too late for RNG)
  kwork_t *work = cancellations;
  while (work != NULL) {
    // For RNG, cancellation is rarely successful
    work = work->next;
  }

  // Process submissions
  work = submissions;
  int submitted = 0;

  while (work != NULL) {
    kwork_t *next = work->next;

    if (work->op == KWORK_OP_RNG_READ) {
      krng_req_t *req = CONTAINER_OF(work, krng_req_t, work);

      // Allocate descriptor
      uint16_t desc_idx = virtqueue_alloc_desc(&rng->vq);

      if (desc_idx == VIRTQUEUE_NO_DESC) {
        // Queue full - immediate failure with backpressure
        kplatform_complete_work(k, work, KERR_BUSY);
        work = next;
        continue;
      }

      // Setup descriptor (device-writable buffer)
      virtqueue_add_desc(&rng->vq, desc_idx, (uint64_t)req->buffer, req->length,
                         VIRTQ_DESC_F_WRITE);

      // Add to available ring
      virtqueue_add_avail(&rng->vq, desc_idx);

      // Track request for completion (constant-time lookup)
      req->platform.desc_idx = desc_idx;
      rng->active_requests[desc_idx] = req;

      // Mark as live
      work->state = KWORK_STATE_LIVE;
      submitted++;
    }

    work = next;
  }

  // Kick device once for all descriptors (bulk submission)
  if (submitted > 0) {
    // CRITICAL: Clean cache to ensure device can see our writes
    // Device reads descriptor table and available ring from RAM, not CPU cache
    size_t desc_size = rng->queue_size * sizeof(virtq_desc_t);
    size_t avail_size = 4 + rng->queue_size * 2 + 2;

    cache_clean_range(rng->vq.desc, desc_size);
    cache_clean_range(rng->vq.avail, avail_size);

    // Write to QUEUE_NOTIFY register to notify device
    mmio_write32(rng->mmio_base, VIRTIO_MMIO_QUEUE_NOTIFY, 0); // Queue 0
  }
}

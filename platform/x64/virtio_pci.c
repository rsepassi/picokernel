// x64 VirtIO PCI Transport Implementation
// VirtIO-RNG device driver and platform_submit

#include "virtio_pci.h"
#include "interrupt.h"
#include "io.h"
#include "pci.h"
#include "platform_impl.h"
#include "printk.h"
#include <stddef.h>

// Memory allocation for virtqueue (static for now)
#define VIRTQUEUE_SIZE 256
static uint8_t g_virtqueue_memory[64 * 1024] __attribute__((aligned(4096)));
static virtio_rng_t g_virtio_rng;

// Memory barrier for x86-64
static inline void mfence(void) { __asm__ volatile("mfence" ::: "memory"); }

// Helper: Write to MMIO address
static void mmio_write32(volatile uint32_t *addr, uint32_t value) {
  *addr = value;
  mfence();
}

// Helper: Read from MMIO address
static uint32_t mmio_read32(volatile uint32_t *addr) {
  mfence();
  return *addr;
}

static uint16_t mmio_read16(volatile uint16_t *addr) {
  mfence();
  return *addr;
}

static uint8_t mmio_read8(volatile uint8_t *addr) {
  mfence();
  return *addr;
}

static void mmio_write8(volatile uint8_t *addr, uint8_t value) {
  *addr = value;
  mfence();
}

static void mmio_write16(volatile uint16_t *addr, uint16_t value) {
  *addr = value;
  mfence();
}

static void mmio_write64(volatile uint64_t *addr, uint64_t value) {
  *addr = value;
  mfence();
}

// Find VirtIO PCI capabilities
static int virtio_find_capabilities(virtio_rng_t *rng) {
  uint8_t cap_offset = pci_config_read8(rng->pci_bus, rng->pci_slot,
                                        rng->pci_func, PCI_REG_CAPABILITIES);

  if (cap_offset == 0) {
    printk("No PCI capabilities found\n");
    return -1;
  }

  int found_common = 0, found_notify = 0, found_isr = 0;

  while (cap_offset != 0) {
    uint8_t cap_id = pci_config_read8(rng->pci_bus, rng->pci_slot,
                                      rng->pci_func, cap_offset);

    if (cap_id == 0x09) { // Vendor-specific capability
      uint8_t cfg_type = pci_config_read8(rng->pci_bus, rng->pci_slot,
                                          rng->pci_func, cap_offset + 3);
      uint8_t bar = pci_config_read8(rng->pci_bus, rng->pci_slot, rng->pci_func,
                                     cap_offset + 4);
      uint32_t offset = pci_config_read32(rng->pci_bus, rng->pci_slot,
                                          rng->pci_func, cap_offset + 8);

      if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
        rng->common_cfg_bar =
            pci_read_bar(rng->pci_bus, rng->pci_slot, rng->pci_func, bar);
        rng->common_cfg_offset = offset;
        rng->common_cfg =
            (volatile virtio_pci_common_cfg_t *)(rng->common_cfg_bar + offset);
        found_common = 1;
      } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
        rng->notify_bar =
            pci_read_bar(rng->pci_bus, rng->pci_slot, rng->pci_func, bar);
        rng->notify_offset = offset;
        rng->notify_off_multiplier = pci_config_read32(
            rng->pci_bus, rng->pci_slot, rng->pci_func, cap_offset + 16);
        rng->notify_base = (volatile uint32_t *)(rng->notify_bar + offset);
        found_notify = 1;
      } else if (cfg_type == VIRTIO_PCI_CAP_ISR_CFG) {
        rng->isr_bar =
            pci_read_bar(rng->pci_bus, rng->pci_slot, rng->pci_func, bar);
        rng->isr_offset = offset;
        rng->isr_status = (volatile uint8_t *)(rng->isr_bar + offset);
        found_isr = 1;
      }
    }

    cap_offset = pci_config_read8(rng->pci_bus, rng->pci_slot, rng->pci_func,
                                  cap_offset + 1);
  }

  return (found_common && found_notify && found_isr) ? 0 : -1;
}

// VirtIO-RNG interrupt handler (minimal - deferred processing pattern)
static void virtio_rng_irq_handler(void *context) {
  virtio_rng_t *rng = (virtio_rng_t *)context;

  // Read ISR status to acknowledge device interrupt
  // Per VirtIO spec, this MUST be read to clear the interrupt line
  volatile uint8_t isr_status = mmio_read8(rng->isr_status);
  (void)isr_status;

  // Set pending flag for deferred processing in ktick
  rng->irq_pending = 1;

  // LAPIC EOI sent by irq_dispatch()
}

// Setup and initialize VirtIO-RNG device
void virtio_rng_setup(platform_t *platform, uint8_t bus, uint8_t slot,
                      uint8_t func) {
  virtio_rng_t *rng = &g_virtio_rng;

  rng->pci_bus = bus;
  rng->pci_slot = slot;
  rng->pci_func = func;
  rng->kernel = platform->kernel;

  // Enable PCI bus mastering and memory access, DISABLE interrupt masking
  uint16_t command = pci_config_read16(bus, slot, func, PCI_REG_COMMAND);
  command |= PCI_CMD_MEM_ENABLE | PCI_CMD_BUS_MASTER;
  command &=
      ~PCI_CMD_INT_DISABLE; // Clear interrupt disable bit (enable interrupts)
  pci_config_write16(bus, slot, func, PCI_REG_COMMAND, command);

  // Find VirtIO capabilities
  if (virtio_find_capabilities(rng) < 0) {
    printk("Failed to find VirtIO capabilities\n");
    return;
  }

  // Reset device
  mmio_write8((volatile uint8_t *)&rng->common_cfg->device_status, 0);

  // Acknowledge device
  mmio_write8((volatile uint8_t *)&rng->common_cfg->device_status,
              VIRTIO_STATUS_ACKNOWLEDGE);

  // Driver ready
  uint8_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
  mmio_write8((volatile uint8_t *)&rng->common_cfg->device_status, status);

  // Read device features
  mmio_write32(&rng->common_cfg->device_feature_select, 0);
  mmio_read32(&rng->common_cfg->device_feature);

  // Write driver features (none needed for basic RNG)
  mmio_write32(&rng->common_cfg->driver_feature_select, 0);
  mmio_write32(&rng->common_cfg->driver_feature, 0);

  // Features OK
  status |= VIRTIO_STATUS_FEATURES_OK;
  mmio_write8((volatile uint8_t *)&rng->common_cfg->device_status, status);

  // Verify features OK
  status = mmio_read8((volatile uint8_t *)&rng->common_cfg->device_status);
  if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
    printk("Device rejected features\n");
    return;
  }

  // Configure device to use legacy interrupts (not MSI-X)
  mmio_write16(&rng->common_cfg->msix_config, 0xFFFF);

  // Setup virtqueue 0 (requestq)
  mmio_write16(&rng->common_cfg->queue_select, 0);
  rng->queue_size = mmio_read16(&rng->common_cfg->queue_size);

  if (rng->queue_size > VIRTQUEUE_SIZE) {
    rng->queue_size = VIRTQUEUE_SIZE;
  }

  // Allocate and initialize virtqueue
  rng->vq_memory = g_virtqueue_memory;
  virtqueue_init(&rng->vq, rng->queue_size, rng->vq_memory);

  // Set queue addresses (physical addresses - in QEMU they're the same as
  // virtual)
  mmio_write64(&rng->common_cfg->queue_desc, (uint64_t)rng->vq.desc);
  mmio_write64(&rng->common_cfg->queue_driver, (uint64_t)rng->vq.avail);
  mmio_write64(&rng->common_cfg->queue_device, (uint64_t)rng->vq.used);

  // Get notify offset for this queue
  uint16_t notify_off = mmio_read16(&rng->common_cfg->queue_notify_off);
  rng->vq.notify_offset = notify_off;

  // Configure interrupts: Use legacy interrupts (not MSI-X)
  mmio_write16(&rng->common_cfg->queue_msix_vector, 0xFFFF);

  // Enable queue
  mmio_write16(&rng->common_cfg->queue_enable, 1);

  // Request tracking and irq_pending flag zeroed by BSS initialization
  rng->irq_pending = 0;

  // Setup interrupt
  uint8_t irq_line = pci_config_read8(bus, slot, func, PCI_REG_INTERRUPT_LINE);
  rng->irq_vector = 32 + irq_line;

  irq_register(rng->irq_vector, virtio_rng_irq_handler, rng);

  // Driver OK
  status |= VIRTIO_STATUS_DRIVER_OK;
  mmio_write8((volatile uint8_t *)&rng->common_cfg->device_status, status);

  // Store in platform
  platform->virtio_rng = rng;

  // Enable IRQ now that everything is set up
  irq_enable(rng->irq_vector);

  printk("VirtIO-RNG initialized successfully\n");
}

// Process deferred interrupt work (called from ktick before callbacks)
void kplatform_tick(platform_t *platform, kernel_t *k) {
  virtio_rng_t *rng = platform->virtio_rng;

  if (rng == NULL) {
    return;
  }

  // Deferred interrupt processing - only process if interrupt occurred
  if (!rng->irq_pending) {
    return;
  }

  rng->irq_pending = 0;

  // Process all used descriptors
  while (virtqueue_has_used(&rng->vq)) {
    uint16_t desc_idx;
    uint32_t len;

    virtqueue_get_used(&rng->vq, &desc_idx, &len);

    // Constant-time lookup
    krng_req_t *req = rng->active_requests[desc_idx];

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
}

// Platform submit function (called from ktick after callbacks)
void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations) {
  virtio_rng_t *rng = platform->virtio_rng;

  if (rng == NULL) {
    // No RNG device, complete all submissions with error
    kwork_t *work = submissions;
    while (work != NULL) {
      kwork_t *next = work->next;
      kplatform_complete_work(platform->kernel, work, KERR_NO_DEVICE);
      work = next;
    }
    return;
  }

  kernel_t *k = platform->kernel;

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
    // Calculate notify address
    uint64_t notify_addr = rng->notify_bar + rng->notify_offset +
                           (rng->vq.notify_offset * rng->notify_off_multiplier);
    volatile uint16_t *notify_ptr = (volatile uint16_t *)notify_addr;

    // Write queue index to notify (this triggers device processing)
    mmio_write16(notify_ptr, 0); // Queue 0
  }
}

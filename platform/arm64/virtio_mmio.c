// ARM64 VirtIO MMIO Platform Glue
// Platform integration for generic VirtIO MMIO transport

#include "virtio_mmio.h"
#include "interrupt.h"
#include "platform_impl.h"
#include "printk.h"
#include "virtio/virtio_mmio.h"
#include "virtio/virtio_rng.h"
#include <stddef.h>

// VirtIO-RNG interrupt handler (minimal - deferred processing pattern)
static void virtio_rng_irq_handler(void *context) {
  virtio_rng_dev_t *rng = (virtio_rng_dev_t *)context;

  printk("! VirtIO-RNG IRQ !\n");

  // Read and acknowledge interrupt status
  virtio_mmio_transport_t *mmio = (virtio_mmio_transport_t *)rng->transport;
  uint32_t isr_status = virtio_mmio_read_isr(mmio);
  printk("ISR status: 0x");
  printk_hex32(isr_status);
  printk("\n");
  virtio_mmio_ack_isr(mmio, isr_status);

  // Set pending flag for deferred processing in ktick
  rng->irq_pending = 1;
}

// Setup and initialize VirtIO-RNG device via MMIO
void virtio_rng_mmio_setup(platform_t *platform, uint64_t mmio_base,
                           uint64_t mmio_size, uint32_t irq_num) {
  (void)mmio_size; // Size not used in generic transport

  printk("VirtIO MMIO device at 0x");
  printk_hex64(mmio_base);
  printk(", IRQ ");
  printk_dec(irq_num);
  printk("\n");

  // Initialize MMIO transport
  if (virtio_mmio_init(&platform->virtio_mmio, (void *)mmio_base) < 0) {
    printk("Failed to initialize VirtIO MMIO transport\n");
    return;
  }

  // Check version
  printk("VirtIO MMIO version: ");
  printk_dec(platform->virtio_mmio.version);
  printk("\n");

  // Check device ID (4 = RNG)
  uint32_t device_id = virtio_mmio_get_device_id(&platform->virtio_mmio);
  printk("Device ID: ");
  printk_dec(device_id);
  printk("\n");

  if (device_id == 0) {
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

  // Initialize RNG device with MMIO transport
  if (virtio_rng_init_mmio(&platform->virtio_rng, &platform->virtio_mmio,
                           platform->virtqueue_memory, platform->kernel) < 0) {
    printk("Failed to initialize VirtIO-RNG device\n");
    return;
  }

  printk("Queue size: ");
  printk_dec(platform->virtio_rng.queue_size);
  printk("\n");

  // Register interrupt handler
  irq_register(irq_num, virtio_rng_irq_handler, &platform->virtio_rng);

  // Mark device as present
  platform->virtio_rng_present = 1;

  // Enable IRQ now that everything is set up
  irq_enable(irq_num);

  // Dump GIC configuration for debugging
  irq_dump_config(irq_num);

  printk("VirtIO-RNG (MMIO) initialized successfully\n");
}

// Process deferred interrupt work (called from ktick before callbacks)
void platform_tick(platform_t *platform, kernel_t *k) {
  if (!platform->virtio_rng_present) {
    return;
  }

  // Delegate to generic RNG interrupt processing
  virtio_rng_process_irq(&platform->virtio_rng, k);
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

  // Process cancellations (best-effort, usually too late for RNG)
  kwork_t *work = cancellations;
  while (work != NULL) {
    // For RNG, cancellation is rarely successful
    work = work->next;
  }

  // Delegate to generic RNG work submission
  virtio_rng_submit_work(&platform->virtio_rng, submissions, platform->kernel);
}

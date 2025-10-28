// x64 VirtIO Platform Integration
// Discovers and initializes VirtIO devices using generic transport layer

#include "kernel.h"
#include "pci.h"
#include "platform_impl.h"
#include "virtio/virtio_pci.h"
#include "virtio/virtio_rng.h"

// Static storage for VirtIO devices
static virtio_pci_transport_t g_virtio_pci_transport;
static virtio_rng_dev_t g_virtio_rng;
static uint8_t g_virtqueue_memory[64 * 1024] __attribute__((aligned(4096)));

// VirtIO-RNG interrupt handler (minimal - deferred processing pattern)
static void virtio_rng_irq_handler(void *context) {
  virtio_rng_dev_t *rng = (virtio_rng_dev_t *)context;

  // Read ISR status to acknowledge device interrupt
  // Per VirtIO spec, this MUST be read to clear the interrupt line
  virtio_pci_transport_t *pci = (virtio_pci_transport_t *)rng->transport;
  (void)virtio_pci_read_isr(pci);

  // Set pending flag for deferred processing in ktick
  rng->irq_pending = 1;

  // LAPIC EOI sent by irq_dispatch()
}

// Forward declaration for pci_scan_devices
void pci_scan_devices(platform_t *platform);

// Setup VirtIO-RNG device
static void virtio_rng_setup(platform_t *platform, uint8_t bus, uint8_t slot,
                             uint8_t func) {
  // Initialize PCI transport
  if (virtio_pci_init(&g_virtio_pci_transport, bus, slot, func) < 0) {
    return;
  }

  // Initialize RNG device
  if (virtio_rng_init_pci(&g_virtio_rng, &g_virtio_pci_transport,
                          g_virtqueue_memory, platform->kernel) < 0) {
    return;
  }

  // Setup interrupt
  uint8_t irq_line =
      platform_pci_config_read8(bus, slot, func, PCI_REG_INTERRUPT_LINE);
  uint32_t irq_vector = 32 + irq_line;

  platform_irq_register(irq_vector, virtio_rng_irq_handler, &g_virtio_rng);
  platform_irq_enable(irq_vector);

  // Store in platform
  platform->virtio_rng = &g_virtio_rng;
}

// Scan PCI bus for VirtIO devices
void pci_scan_devices(platform_t *platform) {
  extern void printk(const char *s);
  extern void printk_dec(uint64_t n);
  extern void printk_hex16(uint16_t n);

  printk("Scanning PCI bus for VirtIO devices...\n");

  int devices_found = 0;

  // Scan first 4 buses only (most systems have devices on bus 0)
  // Scanning all 256 buses takes too long
  for (uint16_t bus = 0; bus < 4; bus++) {
    for (uint8_t slot = 0; slot < 32; slot++) {
      uint32_t vendor_device =
          platform_pci_config_read32(bus, slot, 0, PCI_REG_VENDOR_ID);

      if (vendor_device == 0xFFFFFFFF) {
        continue; // No device
      }

      uint16_t vendor = vendor_device & 0xFFFF;
      uint16_t device = vendor_device >> 16;

      // Check for VirtIO vendor
      if (vendor == VIRTIO_PCI_VENDOR_ID) {
        if (device == VIRTIO_PCI_DEVICE_RNG_LEGACY ||
            device == VIRTIO_PCI_DEVICE_RNG_MODERN) {

          printk("Found VirtIO-RNG at PCI ");
          printk_dec(bus);
          printk(":");
          printk_dec(slot);
          printk(".0 (device ID ");
          printk_hex16(device);
          printk(")\n");

          virtio_rng_setup(platform, bus, slot, 0);
          devices_found++;
        }
      }
    }
  }

  if (devices_found == 0) {
    printk("No VirtIO devices found.\n");
  } else {
    printk("Found ");
    printk_dec(devices_found);
    printk(" VirtIO device(s).\n");
  }
}

// Process deferred interrupt work (called from ktick before callbacks)
void platform_tick(platform_t *platform, kernel_t *k) {
  if (platform->virtio_rng == (void *)0) {
    return;
  }

  virtio_rng_process_irq(platform->virtio_rng, k);
}

// Submit work and cancellations to platform (called from ktick)
void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations) {
  (void)cancellations; // Unused parameter
  if (platform->virtio_rng == (void *)0) {
    // No RNG device, complete all submissions with error
    kwork_t *work = submissions;
    while (work != (void *)0) {
      kwork_t *next = work->next;
      kplatform_complete_work(platform->kernel, work, 5); // KERR_NO_DEVICE
      work = next;
    }
    return;
  }

  // Process cancellations (best-effort, usually too late for RNG)
  // Currently no-op for RNG

  // Process submissions
  virtio_rng_submit_work(platform->virtio_rng, submissions, platform->kernel);
}

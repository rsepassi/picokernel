// RV32 VirtIO Platform Integration
// Discovers and initializes VirtIO devices using generic transport layer

#include "interrupt.h"
#include "kernel.h"
#include "pci.h"
#include "platform_impl.h"
#include "printk.h"
#include "virtio/virtio.h"
#include "virtio/virtio_mmio.h"
#include "virtio/virtio_pci.h"
#include "virtio/virtio_rng.h"

// Static storage for VirtIO devices
// Support both PCI and MMIO transports
static virtio_pci_transport_t g_virtio_pci_transport;
static virtio_mmio_transport_t g_virtio_mmio_transport;
static virtio_rng_dev_t g_virtio_rng;
static uint8_t g_virtqueue_memory[64 * 1024] __attribute__((aligned(4096)));

// VirtIO-RNG interrupt handler (minimal - deferred processing pattern)
static void virtio_rng_irq_handler(void *context) {
  virtio_rng_dev_t *rng = (virtio_rng_dev_t *)context;

  // Read ISR status to acknowledge device interrupt
  // Per VirtIO spec, this MUST be read to clear the interrupt line
  if (rng->transport_type == VIRTIO_TRANSPORT_PCI) {
    virtio_pci_transport_t *pci = (virtio_pci_transport_t *)rng->transport;
    (void)virtio_pci_read_isr(pci);
  } else if (rng->transport_type == VIRTIO_TRANSPORT_MMIO) {
    virtio_mmio_transport_t *mmio = (virtio_mmio_transport_t *)rng->transport;
    uint32_t isr_status = virtio_mmio_read_isr(mmio);
    virtio_mmio_ack_isr(mmio, isr_status);
  }

  // Set pending flag for deferred processing in ktick
  rng->irq_pending = 1;

  // PLIC EOI handled by irq_dispatch()
}

// Forward declarations
void pci_scan_devices(platform_t *platform);
void mmio_scan_devices(platform_t *platform);

// Setup VirtIO-RNG device via PCI
static void virtio_rng_setup(platform_t *platform, uint8_t bus, uint8_t slot,
                             uint8_t func) {
  // Assign BAR addresses (bare-metal RV32 needs to do this manually)
  // Start address for PCI MMIO region (from QEMU virt DT: 0x40000000)
  uint32_t bar_addr = 0x40000000;

  for (int i = 0; i < 6; i++) {
    uint8_t bar_offset = PCI_REG_BAR0 + (i * 4);
    uint32_t bar_val = platform_pci_config_read32(bus, slot, func, bar_offset);

    if (bar_val == 0 || bar_val == 0xFFFFFFFF) {
      continue; // BAR not implemented
    }

    // Skip I/O BARs
    if (bar_val & 0x1) {
      continue;
    }

    // Write all 1s to get BAR size
    platform_pci_config_write16(bus, slot, func, PCI_REG_COMMAND,
                                0); // Disable device
    platform_pci_config_write32(bus, slot, func, bar_offset, 0xFFFFFFFF);
    uint32_t size_mask =
        platform_pci_config_read32(bus, slot, func, bar_offset);
    size_mask &= ~0xF; // Clear flags
    uint32_t size = ~size_mask + 1;

    if (size == 0) {
      continue; // BAR not implemented
    }

    // Check if 64-bit BAR
    uint32_t bar_type = (bar_val >> 1) & 0x3;
    if (bar_type == 0x2) {
      // 64-bit BAR - assign address (only low 32 bits on RV32)
      platform_pci_config_write32(bus, slot, func, bar_offset,
                                  (uint32_t)bar_addr);
      platform_pci_config_write32(bus, slot, func, bar_offset + 4, 0);
      bar_addr += (size + 0xFFF) & ~0xFFF; // Align to 4KB
      i++;                                 // Skip next BAR (high 32 bits)
    } else {
      // 32-bit BAR
      platform_pci_config_write32(bus, slot, func, bar_offset,
                                  (uint32_t)bar_addr);
      bar_addr += (size + 0xFFF) & ~0xFFF;
    }
  }

  // Re-enable device
  uint16_t command =
      platform_pci_config_read16(bus, slot, func, PCI_REG_COMMAND);
  command |= 0x0006; // Memory space + Bus master
  platform_pci_config_write16(bus, slot, func, PCI_REG_COMMAND, command);

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
  uint8_t irq_pin =
      platform_pci_config_read8(bus, slot, func, PCI_REG_INTERRUPT_PIN);

  // RV32 QEMU virt: PCI interrupts use standard INTx swizzling
  // Base IRQ = 1, rotated by (device + pin - 1) % 4
  uint32_t base_irq = 1; // First PCI interrupt
  uint32_t irq_num = base_irq + ((slot + irq_pin - 1) % 4);

  irq_register(irq_num, virtio_rng_irq_handler, &g_virtio_rng);
  irq_enable(irq_num);

  // Store in platform
  platform->virtio_rng = &g_virtio_rng;
}

// Setup VirtIO-RNG device via MMIO
static void virtio_rng_mmio_setup(platform_t *platform, uint32_t mmio_base,
                                  uint32_t mmio_size, uint32_t irq_num) {
  (void)mmio_size; // Size not used in generic transport

  // Initialize MMIO transport
  if (virtio_mmio_init(&g_virtio_mmio_transport, (void *)mmio_base) < 0) {
    return;
  }

  // Verify device ID
  uint32_t device_id = virtio_mmio_get_device_id(&g_virtio_mmio_transport);
  if (device_id != VIRTIO_ID_RNG) {
    return;
  }

  // Initialize RNG device with MMIO transport
  if (virtio_rng_init_mmio(&g_virtio_rng, &g_virtio_mmio_transport,
                           g_virtqueue_memory, platform->kernel) < 0) {
    return;
  }

  // Setup interrupt
  irq_register(irq_num, virtio_rng_irq_handler, &g_virtio_rng);
  irq_enable(irq_num);

  // Store in platform
  platform->virtio_rng = &g_virtio_rng;
}

// Helper to get device type name
static const char *virtio_device_name(uint16_t device_id) {
  switch (device_id) {
  case VIRTIO_PCI_DEVICE_NET_LEGACY:
  case VIRTIO_PCI_DEVICE_NET_MODERN:
    return "VirtIO-Net";
  case VIRTIO_PCI_DEVICE_BLOCK_LEGACY:
  case VIRTIO_PCI_DEVICE_BLOCK_MODERN:
    return "VirtIO-Block";
  case VIRTIO_PCI_DEVICE_RNG_LEGACY:
  case VIRTIO_PCI_DEVICE_RNG_MODERN:
    return "VirtIO-RNG";
  default:
    return "VirtIO-Unknown";
  }
}

// Scan PCI bus for VirtIO devices
void pci_scan_devices(platform_t *platform) {
  printk("Scanning PCI bus for VirtIO devices...\n");

  int devices_found = 0;
  int rng_initialized = 0;

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
        // Check if this is any VirtIO device (0x1000-0x107F range)
        if ((device >= 0x1000 && device <= 0x107F)) {
          printk("Found ");
          printk(virtio_device_name(device));
          printk(" at PCI ");
          printk_dec(bus);
          printk(":");
          printk_dec(slot);
          printk(".0 (device ID 0x");
          printk_hex16(device);
          printk(")\n");

          devices_found++;

          // Initialize RNG device if found and not already initialized
          if (!rng_initialized && (device == VIRTIO_PCI_DEVICE_RNG_LEGACY ||
                                   device == VIRTIO_PCI_DEVICE_RNG_MODERN)) {
            virtio_rng_setup(platform, bus, slot, 0);
            rng_initialized = 1;
          }
        }
      }
    }
  }

  if (devices_found == 0) {
    printk("No VirtIO devices found.\n");
  } else {
    printk("Found ");
    printk_dec(devices_found);
    printk(" VirtIO device(s) total.\n");
  }
}

// Process deferred interrupt work (called from ktick before callbacks)
void platform_tick(platform_t *platform, kernel_t *k) {
  if (platform->virtio_rng == (void *)0) {
    return;
  }

  // POLLING FALLBACK: If no interrupts, check device manually
  // This works around interrupt delivery issues on MMIO devices
  if (!platform->virtio_rng->irq_pending) {
    // Check if device has completed work (poll used ring)
    if (virtqueue_has_used(&platform->virtio_rng->vq)) {
      platform->virtio_rng->irq_pending = 1;
    }
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

// Helper to get VirtIO device type name from device ID
static const char *virtio_mmio_device_name(uint32_t device_id) {
  switch (device_id) {
  case VIRTIO_ID_NET:
    return "VirtIO-Net";
  case VIRTIO_ID_BLOCK:
    return "VirtIO-Block";
  case VIRTIO_ID_RNG:
    return "VirtIO-RNG";
  default:
    return "VirtIO-Unknown";
  }
}

// Probe for VirtIO MMIO devices at known addresses
// QEMU RISC-V virt machine uses device tree for discovery
void mmio_scan_devices(platform_t *platform) {
  printk("Probing for VirtIO MMIO devices...\n");

// QEMU RISC-V virt VirtIO MMIO region layout
// QEMU places devices starting at 0x10001000 with variable spacing
// In practice, device tree should be used for discovery
#define VIRTIO_MMIO_BASE 0x10001000UL
#define VIRTIO_MMIO_DEVICE_STRIDE 0x1000 // 4KB per device
#define VIRTIO_MMIO_MAX_DEVICES 8
#define VIRTIO_MMIO_IRQ_BASE 1       // IRQs start at 1 for VirtIO MMIO
#define VIRTIO_MMIO_MAGIC 0x74726976 // "virt" in little-endian

  int devices_found = 0;
  int rng_initialized = 0;

  // Scan for devices
  for (int i = 0; i < VIRTIO_MMIO_MAX_DEVICES; i++) {
    uint32_t base = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_DEVICE_STRIDE);

    // Read magic value
    volatile uint32_t *magic_ptr = (volatile uint32_t *)base;
    uint32_t magic = *magic_ptr;

    if (magic != VIRTIO_MMIO_MAGIC) {
      continue; // No device at this slot
    }

    // Read device ID at offset 0x08
    volatile uint32_t *device_id_ptr = (volatile uint32_t *)(base + 0x08);
    uint32_t device_id = *device_id_ptr;

    // Device ID 0 means empty slot
    if (device_id == 0) {
      continue;
    }

    printk("Found ");
    printk(virtio_mmio_device_name(device_id));
    printk(" at MMIO 0x");
    printk_hex32(base);
    printk(" (device ID ");
    printk_dec(device_id);
    printk(")");
    printk("\n");

    devices_found++;

    // Check if device is already initialized (from PCI)
    if (platform->virtio_rng != (void *)0) {
      continue;
    }

    // Initialize RNG device if found
    if (device_id == VIRTIO_ID_RNG && !rng_initialized) {
      uint32_t irq = VIRTIO_MMIO_IRQ_BASE + i;
      virtio_rng_mmio_setup(platform, base, VIRTIO_MMIO_DEVICE_STRIDE, irq);
      rng_initialized = 1;
    }
  }

  if (devices_found == 0) {
    printk("No VirtIO MMIO devices found.\n");
  } else {
    printk("Found ");
    printk_dec(devices_found);
    printk(" VirtIO MMIO device(s) total.\n");
  }
}

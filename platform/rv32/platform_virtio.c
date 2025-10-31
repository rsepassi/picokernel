// RV32 VirtIO Platform Integration
// Discovers and initializes VirtIO devices using generic transport layer

#include "interrupt.h"
#include "kernel.h"
#include "pci.h"
#include "platform_impl.h"
#include "printk.h"
#include "virtio/virtio.h"
#include "virtio/virtio_blk.h"
#include "virtio/virtio_mmio.h"
#include "virtio/virtio_net.h"
#include "virtio/virtio_pci.h"
#include "virtio/virtio_rng.h"

// Dispatch wrapper for virtio_rng_process_irq (type-safe function pointer)
static void virtio_rng_process_irq_dispatch(void *dev, kernel_t *k) {
  virtio_rng_dev_t *rng = (virtio_rng_dev_t *)dev;
  virtio_rng_process_irq(rng, k);
}

// Dispatch wrapper for virtio_blk_process_irq (type-safe function pointer)
static void virtio_blk_process_irq_dispatch(void *dev, kernel_t *k) {
  virtio_blk_dev_t *blk = (virtio_blk_dev_t *)dev;
  virtio_blk_process_irq(blk, k);
}

// Dispatch wrapper for virtio_net_process_irq (type-safe function pointer)
static void virtio_net_process_irq_dispatch(void *dev, kernel_t *k) {
  virtio_net_dev_t *net = (virtio_net_dev_t *)dev;
  virtio_net_process_irq(net, k);
}

// VirtIO-RNG ISR acknowledgment
static bool virtio_rng_ack_isr(void *dev) {
  virtio_rng_dev_t *rng = (virtio_rng_dev_t *)dev;

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

  return true; // Always process RNG interrupts
}

// VirtIO-BLK ISR acknowledgment
static bool virtio_blk_ack_isr(void *dev) {
  virtio_blk_dev_t *blk = (virtio_blk_dev_t *)dev;

  // Read ISR status to acknowledge device interrupt
  if (blk->transport_type == VIRTIO_TRANSPORT_PCI) {
    virtio_pci_transport_t *pci = (virtio_pci_transport_t *)blk->transport;
    (void)virtio_pci_read_isr(pci);
  } else if (blk->transport_type == VIRTIO_TRANSPORT_MMIO) {
    virtio_mmio_transport_t *mmio = (virtio_mmio_transport_t *)blk->transport;
    uint32_t isr_status = virtio_mmio_read_isr(mmio);
    virtio_mmio_ack_isr(mmio, isr_status);
  }

  return true; // Always process BLK interrupts
}

// VirtIO-NET ISR acknowledgment
static bool virtio_net_ack_isr(void *dev) {
  virtio_net_dev_t *net = (virtio_net_dev_t *)dev;

  // Read ISR status to acknowledge device interrupt
  if (net->transport_type == VIRTIO_TRANSPORT_PCI) {
    virtio_pci_transport_t *pci = (virtio_pci_transport_t *)net->transport;
    (void)virtio_pci_read_isr(pci);
  } else if (net->transport_type == VIRTIO_TRANSPORT_MMIO) {
    virtio_mmio_transport_t *mmio = (virtio_mmio_transport_t *)net->transport;
    uint32_t isr_status = virtio_mmio_read_isr(mmio);
    virtio_mmio_ack_isr(mmio, isr_status);
  }

  return true; // Always process NET interrupts
}

// Shared VirtIO interrupt handler (used for all VirtIO devices)
static void virtio_irq_handler(void *context) {
  kdevice_base_t *dev = (kdevice_base_t *)context;
  platform_t *platform = (platform_t *)dev->platform;

  // NOTE: Cannot use printk from IRQ context - it's not reentrant
  // Use global variables for debugging instead

  // Device-specific ISR acknowledgment
  // Returns true if interrupt should be processed
  if (dev->ack_isr(context)) {
    // Enqueue device pointer for deferred processing
    kirq_ring_enqueue(&platform->irq_ring, context);
  }

  // PLIC EOI sent by irq_dispatch()
}

// Forward declarations
void pci_scan_devices(platform_t *platform);
void mmio_scan_devices(platform_t *platform);

// Helper function to allocate PCI BARs for a device
// This handles both 32-bit and 64-bit BARs, skipping I/O BARs
static void allocate_pci_bars(platform_t *platform, uint8_t bus, uint8_t slot,
                              uint8_t func, const char *device_name) {
  printk("[");
  printk(device_name);
  printk("] Allocating BARs starting at 0x");
  printk_hex32(platform->pci_next_bar_addr);
  printk("\n");

  for (int i = 0; i < 6; i++) {
    uint8_t bar_offset = PCI_REG_BAR0 + (i * 4);
    uint32_t bar_val =
        platform_pci_config_read32(platform, bus, slot, func, bar_offset);

    if (bar_val == 0 || bar_val == 0xFFFFFFFF) {
      continue; // BAR not implemented
    }

    // Skip I/O BARs
    if (bar_val & 0x1) {
      continue;
    }

    // Write all 1s to get BAR size
    platform_pci_config_write16(platform, bus, slot, func, PCI_REG_COMMAND,
                                0); // Disable device
    platform_pci_config_write32(platform, bus, slot, func, bar_offset,
                                0xFFFFFFFF);
    uint32_t size_mask =
        platform_pci_config_read32(platform, bus, slot, func, bar_offset);
    size_mask &= ~0xF; // Clear flags
    uint32_t size = ~size_mask + 1;

    if (size == 0) {
      continue; // BAR not implemented
    }

    // Check if 64-bit BAR
    uint32_t bar_type = (bar_val >> 1) & 0x3;
    if (bar_type == 0x2) {
      // 64-bit BAR - assign address (only low 32 bits on RV32)
      platform_pci_config_write32(platform, bus, slot, func, bar_offset,
                                  platform->pci_next_bar_addr);
      platform_pci_config_write32(platform, bus, slot, func, bar_offset + 4,
                                  0); // High 32 bits = 0
      platform->pci_next_bar_addr += (size + 0xFFF) & ~0xFFF; // Align to 4KB
      i++;                                                    // Skip next BAR
    } else {
      // 32-bit BAR
      platform_pci_config_write32(platform, bus, slot, func, bar_offset,
                                  platform->pci_next_bar_addr);
      platform->pci_next_bar_addr += (size + 0xFFF) & ~0xFFF;
    }
  }

  printk("[");
  printk(device_name);
  printk("] BARs allocated, next address: 0x");
  printk_hex32(platform->pci_next_bar_addr);
  printk("\n");
}

// Setup VirtIO-RNG device via PCI
static void virtio_rng_setup(platform_t *platform, uint8_t bus, uint8_t slot,
                             uint8_t func) {
  // Assign BAR addresses (bare-metal RV32 needs to do this manually)
  allocate_pci_bars(platform, bus, slot, func, "RNG");

  // Re-enable device
  uint16_t command =
      platform_pci_config_read16(platform, bus, slot, func, PCI_REG_COMMAND);
  command |= 0x0006; // Memory space + Bus master
  platform_pci_config_write16(platform, bus, slot, func, PCI_REG_COMMAND,
                              command);

  // Initialize PCI transport for RNG
  if (virtio_pci_init(&platform->virtio_pci_transport_rng, platform, bus, slot,
                      func) < 0) {
    return;
  }

  // Initialize RNG device
  if (virtio_rng_init_pci(
          &platform->virtio_rng, &platform->virtio_pci_transport_rng,
          &platform->virtqueue_rng_memory, platform->kernel) < 0) {
    return;
  }

  // Initialize device base fields
  platform->virtio_rng.base.device_type = KDEVICE_TYPE_VIRTIO_RNG;
  platform->virtio_rng.base.platform = platform;
  platform->virtio_rng.base.process_irq = virtio_rng_process_irq_dispatch;
  platform->virtio_rng.base.ack_isr = virtio_rng_ack_isr;

  // Setup interrupt
  uint8_t irq_pin = platform_pci_config_read8(platform, bus, slot, func,
                                              PCI_REG_INTERRUPT_PIN);

  // RV32 QEMU virt: PCI interrupts use standard INTx swizzling
  // Base IRQ = 1, rotated by (device + pin - 1) % 4
  uint32_t base_irq = 1; // First PCI interrupt
  uint32_t irq_num = base_irq + ((slot + irq_pin - 1) % 4);

  platform_irq_register(platform, irq_num, virtio_irq_handler,
                        &platform->virtio_rng);
  platform_irq_enable(platform, irq_num);

  // Store pointer to active RNG device
  platform->virtio_rng_ptr = &platform->virtio_rng;
}

// Setup VirtIO-RNG device via MMIO
static void virtio_rng_mmio_setup(platform_t *platform, uint32_t mmio_base,
                                  uint32_t mmio_size, uint32_t irq_num) {
  (void)mmio_size; // Size not used in generic transport

  // Initialize MMIO transport
  if (virtio_mmio_init(&platform->virtio_mmio_transport_rng,
                       (void *)mmio_base) < 0) {
    return;
  }

  // Verify device ID
  uint32_t device_id =
      virtio_mmio_get_device_id(&platform->virtio_mmio_transport_rng);
  if (device_id != VIRTIO_ID_RNG) {
    return;
  }

  // Initialize RNG device with MMIO transport
  if (virtio_rng_init_mmio(
          &platform->virtio_rng, &platform->virtio_mmio_transport_rng,
          &platform->virtqueue_rng_memory, platform->kernel) < 0) {
    return;
  }

  // Initialize device base fields
  platform->virtio_rng.base.device_type = KDEVICE_TYPE_VIRTIO_RNG;
  platform->virtio_rng.base.platform = platform;
  platform->virtio_rng.base.process_irq = virtio_rng_process_irq_dispatch;
  platform->virtio_rng.base.ack_isr = virtio_rng_ack_isr;

  // Setup interrupt
  platform_irq_register(platform, irq_num, virtio_irq_handler,
                        &platform->virtio_rng);
  platform_irq_enable(platform, irq_num);

  // Store pointer to active RNG device
  platform->virtio_rng_ptr = &platform->virtio_rng;
}

// Setup VirtIO-BLK device via PCI
static void virtio_blk_setup(platform_t *platform, uint8_t bus, uint8_t slot,
                             uint8_t func) {
  // Assign BAR addresses (bare-metal RV32 needs to do this manually)
  allocate_pci_bars(platform, bus, slot, func, "BLK");

  // Re-enable device
  uint16_t command =
      platform_pci_config_read16(platform, bus, slot, func, PCI_REG_COMMAND);
  command |= 0x0006;
  platform_pci_config_write16(platform, bus, slot, func, PCI_REG_COMMAND,
                              command);

  // Initialize PCI transport for BLK
  if (virtio_pci_init(&platform->virtio_pci_transport_blk, platform, bus, slot,
                      func) < 0) {
    return;
  }

  // Initialize BLK device
  if (virtio_blk_init_pci(
          &platform->virtio_blk, &platform->virtio_pci_transport_blk,
          &platform->virtqueue_blk_memory, platform->kernel) < 0) {
    return;
  }

  // Initialize device base fields
  platform->virtio_blk.base.device_type = KDEVICE_TYPE_VIRTIO_BLK;
  platform->virtio_blk.base.platform = platform;
  platform->virtio_blk.base.process_irq = virtio_blk_process_irq_dispatch;
  platform->virtio_blk.base.ack_isr = virtio_blk_ack_isr;

  // Setup interrupt
  uint8_t irq_pin = platform_pci_config_read8(platform, bus, slot, func,
                                              PCI_REG_INTERRUPT_PIN);

  uint32_t base_irq = 1;
  uint32_t irq_num = base_irq + ((slot + irq_pin - 1) % 4);

  platform_irq_register(platform, irq_num, virtio_irq_handler,
                        &platform->virtio_blk);
  platform_irq_enable(platform, irq_num);

  // Store device info
  platform->virtio_blk_ptr = &platform->virtio_blk;
  platform->has_block_device = true;
  platform->block_sector_size = platform->virtio_blk.sector_size;
  platform->block_capacity = platform->virtio_blk.capacity;

  // Log device info
  printk("  sector_size=");
  printk_dec(platform->block_sector_size);
  printk(" capacity=");
  printk_dec(platform->block_capacity);
  printk(" sectors (");
  uint64_t mb =
      (platform->block_capacity * platform->block_sector_size) / (1024 * 1024);
  printk_dec(mb);
  printk(" MB)\n");
}

// Setup VirtIO-BLK device via MMIO
static void virtio_blk_mmio_setup(platform_t *platform, uint32_t mmio_base,
                                  uint32_t mmio_size, uint32_t irq_num) {
  (void)mmio_size;

  // Initialize MMIO transport
  if (virtio_mmio_init(&platform->virtio_mmio_transport_blk,
                       (void *)mmio_base) < 0) {
    return;
  }

  // Verify device ID
  uint32_t device_id =
      virtio_mmio_get_device_id(&platform->virtio_mmio_transport_blk);
  if (device_id != VIRTIO_ID_BLOCK) {
    return;
  }

  // Initialize BLK device with MMIO transport
  if (virtio_blk_init_mmio(
          &platform->virtio_blk, &platform->virtio_mmio_transport_blk,
          &platform->virtqueue_blk_memory, platform->kernel) < 0) {
    return;
  }

  // Initialize device base fields
  platform->virtio_blk.base.device_type = KDEVICE_TYPE_VIRTIO_BLK;
  platform->virtio_blk.base.platform = platform;
  platform->virtio_blk.base.process_irq = virtio_blk_process_irq_dispatch;
  platform->virtio_blk.base.ack_isr = virtio_blk_ack_isr;

  // Setup interrupt
  platform_irq_register(platform, irq_num, virtio_irq_handler,
                        &platform->virtio_blk);
  platform_irq_enable(platform, irq_num);

  // Store device info
  platform->virtio_blk_ptr = &platform->virtio_blk;
  platform->has_block_device = true;
  platform->block_sector_size = platform->virtio_blk.sector_size;
  platform->block_capacity = platform->virtio_blk.capacity;

  // Log device info
  printk("  sector_size=");
  printk_dec(platform->block_sector_size);
  printk(" capacity=");
  printk_dec(platform->block_capacity);
  printk(" sectors (");
  uint64_t mb =
      (platform->block_capacity * platform->block_sector_size) / (1024 * 1024);
  printk_dec(mb);
  printk(" MB)\n");
}

// Setup VirtIO-NET device via PCI
static void virtio_net_setup(platform_t *platform, uint8_t bus, uint8_t slot,
                             uint8_t func) {
  // Assign BAR addresses (bare-metal RV32 needs to do this manually)
  allocate_pci_bars(platform, bus, slot, func, "NET");

  // Re-enable device
  uint16_t command =
      platform_pci_config_read16(platform, bus, slot, func, PCI_REG_COMMAND);
  command |= 0x0006; // Memory space + Bus master
  platform_pci_config_write16(platform, bus, slot, func, PCI_REG_COMMAND,
                              command);

  // Initialize PCI transport for NET
  if (virtio_pci_init(&platform->virtio_pci_transport_net, platform, bus, slot,
                      func) < 0) {
    return;
  }

  // Initialize NET device
  if (virtio_net_init_pci(
          &platform->virtio_net, &platform->virtio_pci_transport_net,
          &platform->virtqueue_net_rx_memory,
          &platform->virtqueue_net_tx_memory, platform->kernel) < 0) {
    return;
  }

  // Initialize device base fields
  platform->virtio_net.base.device_type = KDEVICE_TYPE_VIRTIO_NET;
  platform->virtio_net.base.platform = platform;
  platform->virtio_net.base.process_irq = virtio_net_process_irq_dispatch;
  platform->virtio_net.base.ack_isr = virtio_net_ack_isr;

  // Setup interrupt
  uint8_t irq_pin = platform_pci_config_read8(platform, bus, slot, func,
                                              PCI_REG_INTERRUPT_PIN);

  uint32_t base_irq = 1;
  uint32_t irq_num = base_irq + ((slot + irq_pin - 1) % 4);

  platform_irq_register(platform, irq_num, virtio_irq_handler,
                        &platform->virtio_net);
  platform_irq_enable(platform, irq_num);

  // Store device info
  platform->virtio_net_ptr = &platform->virtio_net;
  platform->has_net_device = true;

  // Copy MAC address
  for (int i = 0; i < 6; i++) {
    platform->net_mac_address[i] = platform->virtio_net.mac_address[i];
  }

  // Log device info
  printk("  mac=");
  for (int i = 0; i < 6; i++) {
    if (i > 0)
      printk(":");
    uint8_t b = platform->net_mac_address[i];
    uint8_t hi = (b >> 4) & 0xF;
    uint8_t lo = b & 0xF;
    printk_putc(hi < 10 ? '0' + hi : 'a' + (hi - 10));
    printk_putc(lo < 10 ? '0' + lo : 'a' + (lo - 10));
  }
  printk("\n");
}

// Setup VirtIO-NET device via MMIO
static void virtio_net_mmio_setup(platform_t *platform, uint32_t mmio_base,
                                  uint32_t mmio_size, uint32_t irq_num) {
  (void)mmio_size;

  // Initialize MMIO transport
  if (virtio_mmio_init(&platform->virtio_mmio_transport_net,
                       (void *)mmio_base) < 0) {
    return;
  }

  // Verify device ID
  uint32_t device_id =
      virtio_mmio_get_device_id(&platform->virtio_mmio_transport_net);
  if (device_id != VIRTIO_ID_NET) {
    return;
  }

  // Initialize NET device with MMIO transport
  if (virtio_net_init_mmio(
          &platform->virtio_net, &platform->virtio_mmio_transport_net,
          &platform->virtqueue_net_rx_memory,
          &platform->virtqueue_net_tx_memory, platform->kernel) < 0) {
    return;
  }

  // Initialize device base fields
  platform->virtio_net.base.device_type = KDEVICE_TYPE_VIRTIO_NET;
  platform->virtio_net.base.platform = platform;
  platform->virtio_net.base.process_irq = virtio_net_process_irq_dispatch;
  platform->virtio_net.base.ack_isr = virtio_net_ack_isr;

  // Setup interrupt
  platform_irq_register(platform, irq_num, virtio_irq_handler,
                        &platform->virtio_net);
  platform_irq_enable(platform, irq_num);

  // Store device info
  platform->virtio_net_ptr = &platform->virtio_net;
  platform->has_net_device = true;

  // Copy MAC address
  for (int i = 0; i < 6; i++) {
    platform->net_mac_address[i] = platform->virtio_net.mac_address[i];
  }

  // Log device info
  printk("  mac=");
  for (int i = 0; i < 6; i++) {
    if (i > 0)
      printk(":");
    uint8_t b = platform->net_mac_address[i];
    uint8_t hi = (b >> 4) & 0xF;
    uint8_t lo = b & 0xF;
    printk_putc(hi < 10 ? '0' + hi : 'a' + (hi - 10));
    printk_putc(lo < 10 ? '0' + lo : 'a' + (lo - 10));
  }
  printk("\n");
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
  int blk_initialized = 0;
  int net_initialized = 0;

  // Scan first 4 buses only (most systems have devices on bus 0)
  // Scanning all 256 buses takes too long
  for (uint16_t bus = 0; bus < 4; bus++) {
    for (uint8_t slot = 0; slot < 32; slot++) {
      uint32_t vendor_device =
          platform_pci_config_read32(platform, bus, slot, 0, PCI_REG_VENDOR_ID);

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

          // Initialize BLK device if found and not already initialized
          if (!blk_initialized && (device == VIRTIO_PCI_DEVICE_BLOCK_LEGACY ||
                                   device == VIRTIO_PCI_DEVICE_BLOCK_MODERN)) {
            virtio_blk_setup(platform, bus, slot, 0);
            blk_initialized = 1;
          }

          // Initialize NET device if found and not already initialized
          if (!net_initialized && (device == VIRTIO_PCI_DEVICE_NET_LEGACY ||
                                   device == VIRTIO_PCI_DEVICE_NET_MODERN)) {
            virtio_net_setup(platform, bus, slot, 0);
            net_initialized = 1;
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
  // Check for IRQ ring overflows (dropped interrupts)
  uint32_t current_overflow = kirq_ring_overflow_count(&platform->irq_ring);
  if (current_overflow > platform->last_overflow_count) {
    uint32_t dropped = current_overflow - platform->last_overflow_count;

    // Log every 100 overflows to avoid spam
    if (current_overflow % 100 == 0 || platform->last_overflow_count == 0) {
      printk("WARNING: IRQ ring overflows: ");
      printk_dec(current_overflow);
      printk(" (");
      printk_dec(dropped);
      printk(" dropped interrupts)\n");
    }

    platform->last_overflow_count = current_overflow;
  }

  // Snapshot the current write position to avoid infinite loop if devices
  // re-enqueue Only process interrupts that were pending at the start of this
  // tick
  uint32_t end_pos = kirq_ring_snapshot(&platform->irq_ring);

  // Process pending device IRQs up to the captured end position
  void *dev_ptr;
  while ((dev_ptr = kirq_ring_dequeue_bounded(&platform->irq_ring, end_pos)) !=
         NULL) {
    // Cast to device base pointer
    kdevice_base_t *dev = (kdevice_base_t *)dev_ptr;

    // Call device-specific interrupt handler via function pointer
    // Device may re-enqueue itself for next tick
    dev->process_irq(dev, k);
  }
}

// Submit work and cancellations to platform (called from ktick)
void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations) {
  // Handle cancellations first
  // Only NET_RECV (standing work) supports cancellation
  // RNG, BLK, NET_SEND (one-shot operations) do not support cancellation
  if (cancellations != NULL) {
    kwork_t *work = cancellations;
    while (work != NULL) {
      kwork_t *next = work->next;

      // Route network cancellations to network device
      // virtio_net_cancel_work will call kplatform_cancel_work for NET_RECV
      // only
      if ((work->op == KWORK_OP_NET_RECV || work->op == KWORK_OP_NET_SEND) &&
          platform->virtio_net_ptr != NULL) {
        virtio_net_cancel_work(platform->virtio_net_ptr, work,
                               platform->kernel);
      }
      // For all other operations (RNG, BLK, TIMER), cancellation is not
      // supported Silently ignore the request - do not call
      // kplatform_cancel_work

      work = next;
    }
  }

  // Separate submissions by device type
  kwork_t *rng_work = NULL;
  kwork_t *blk_work = NULL;
  kwork_t *net_work = NULL;
  kwork_t *unknown_work = NULL;

  kwork_t *work = submissions;
  while (work != NULL) {
    kwork_t *next = work->next;
    work->next = NULL; // Clear next pointer for re-linking

    if (work->op == KWORK_OP_RNG_READ) {
      // Link to RNG list
      work->next = rng_work;
      rng_work = work;
    } else if (work->op == KWORK_OP_BLOCK_READ ||
               work->op == KWORK_OP_BLOCK_WRITE ||
               work->op == KWORK_OP_BLOCK_FLUSH) {
      // Link to BLK list
      work->next = blk_work;
      blk_work = work;
    } else if (work->op == KWORK_OP_NET_RECV || work->op == KWORK_OP_NET_SEND) {
      // Link to NET list
      work->next = net_work;
      net_work = work;
    } else {
      // Unknown operation
      work->next = unknown_work;
      unknown_work = work;
    }

    work = next;
  }

  // Submit RNG work
  if (rng_work != NULL) {
    if (platform->virtio_rng_ptr == NULL) {
      // No RNG device, complete all with error
      work = rng_work;
      while (work != NULL) {
        kwork_t *next = work->next;
        kplatform_complete_work(platform->kernel, work, KERR_NO_DEVICE);
        work = next;
      }
    } else {
      virtio_rng_submit_work(platform->virtio_rng_ptr, rng_work,
                             platform->kernel);
    }
  }

  // Submit BLK work
  if (blk_work != NULL) {
    if (platform->virtio_blk_ptr == NULL) {
      // No BLK device, complete all with error
      work = blk_work;
      while (work != NULL) {
        kwork_t *next = work->next;
        kplatform_complete_work(platform->kernel, work, KERR_NO_DEVICE);
        work = next;
      }
    } else {
      virtio_blk_submit_work(platform->virtio_blk_ptr, blk_work,
                             platform->kernel);
    }
  }

  // Submit NET work
  if (net_work != NULL) {
    if (platform->virtio_net_ptr == NULL) {
      // No NET device, complete all with error
      work = net_work;
      while (work != NULL) {
        kwork_t *next = work->next;
        kplatform_complete_work(platform->kernel, work, KERR_NO_DEVICE);
        work = next;
      }
    } else {
      virtio_net_submit_work(platform->virtio_net_ptr, net_work,
                             platform->kernel);
    }
  }

  // Complete unknown work with error
  if (unknown_work != NULL) {
    work = unknown_work;
    while (work != NULL) {
      kwork_t *next = work->next;
      kplatform_complete_work(platform->kernel, work, KERR_INVALID);
      work = next;
    }
  }
}

// Release a network receive buffer back to the ring (for standing work)
void platform_net_buffer_release(platform_t *platform, void *req,
                                 size_t buffer_index) {
  // Validate platform has network device
  if (platform == NULL || platform->virtio_net_ptr == NULL) {
    return;
  }

  // Call network device buffer release
  virtio_net_buffer_release(platform->virtio_net_ptr, req, buffer_index);
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
  int blk_initialized = 0;
  int net_initialized = 0;

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

    // Initialize RNG device if found and not already initialized
    if (device_id == VIRTIO_ID_RNG && !rng_initialized &&
        platform->virtio_rng_ptr == NULL) {
      uint32_t irq = VIRTIO_MMIO_IRQ_BASE + i;
      virtio_rng_mmio_setup(platform, base, VIRTIO_MMIO_DEVICE_STRIDE, irq);
      rng_initialized = 1;
    }

    // Initialize BLK device if found and not already initialized
    if (device_id == VIRTIO_ID_BLOCK && !blk_initialized &&
        platform->virtio_blk_ptr == NULL) {
      uint32_t irq = VIRTIO_MMIO_IRQ_BASE + i;
      virtio_blk_mmio_setup(platform, base, VIRTIO_MMIO_DEVICE_STRIDE, irq);
      blk_initialized = 1;
    }

    // Initialize NET device if found and not already initialized
    if (device_id == VIRTIO_ID_NET && !net_initialized &&
        platform->virtio_net_ptr == NULL) {
      uint32_t irq = VIRTIO_MMIO_IRQ_BASE + i;
      virtio_net_mmio_setup(platform, base, VIRTIO_MMIO_DEVICE_STRIDE, irq);
      net_initialized = 1;
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

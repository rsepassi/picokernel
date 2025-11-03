// Shared VirtIO Platform Integration
// Discovers and initializes VirtIO devices using generic transport layer
// Platform must provide: platform_pci_irq_swizzle(),
// platform_mmio_irq_number(), and VIRTIO_MMIO_* defines

#ifdef __x86_64__
#include "acpi.h"
#endif
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

  // Platform interrupt controller EOI sent by irq_dispatch()
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
  printk_hex64(platform->pci_next_bar_addr);
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
      printk("[");
      printk(device_name);
      printk("] BAR");
      printk_dec(i);
      printk(": I/O BAR (skipped)\n");
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
      // 64-bit BAR - assign address
      // Align address to BAR size (required by PCI spec)
      platform->pci_next_bar_addr = (platform->pci_next_bar_addr + size - 1) & ~((uint64_t)size - 1);
      uint64_t bar_addr = platform->pci_next_bar_addr;
      // Preserve BAR type bits (bits 3:0) when writing address
      // Bit 0=0 (memory), bits 2:1=10 (64-bit), bit 3=prefetchable
      uint32_t bar_flags = bar_val & 0xF;
      platform_pci_config_write32(platform, bus, slot, func, bar_offset,
                                  (uint32_t)bar_addr | bar_flags);
      platform_pci_config_write32(platform, bus, slot, func, bar_offset + 4,
                                  (uint32_t)(bar_addr >> 32));

      printk("[");
      printk(device_name);
      printk("] BAR");
      printk_dec(i);
      printk("+");
      printk_dec(i+1);
      printk(": 64-bit, size=");
      printk_dec(size);
      printk(" bytes, assigned 0x");
      printk_hex64(bar_addr);
      printk("\n");

      platform->pci_next_bar_addr += (size + 0xFFF) & ~0xFFFULL; // Align to 4KB
      i++; // Skip next BAR (high 32 bits)
    } else {
      // 32-bit BAR
      // Align address to BAR size (required by PCI spec)
      platform->pci_next_bar_addr = (platform->pci_next_bar_addr + size - 1) & ~((uint64_t)size - 1);
      uint64_t bar_addr = platform->pci_next_bar_addr;
      // Preserve BAR type bits (bits 3:0) when writing address
      uint32_t bar_flags = bar_val & 0xF;
      platform_pci_config_write32(platform, bus, slot, func, bar_offset,
                                  (uint32_t)bar_addr | bar_flags);

      printk("[");
      printk(device_name);
      printk("] BAR");
      printk_dec(i);
      printk(": 32-bit, size=");
      printk_dec(size);
      printk(" bytes, assigned 0x");
      printk_hex64(bar_addr);
      printk("\n");

      platform->pci_next_bar_addr += (size + 0xFFF) & ~0xFFFULL;
    }
  }

  printk("[");
  printk(device_name);
  printk("] BARs allocated, next address: 0x");
  printk_hex64(platform->pci_next_bar_addr);
  printk("\n");
}

// Setup VirtIO-RNG device via PCI
static void virtio_rng_setup(platform_t *platform, uint8_t bus, uint8_t slot,
                             uint8_t func) {
  // Assign BAR addresses (bare-metal platforms need to do this manually)
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

#ifdef __x86_64__
  // x64: Use MSI-X interrupts
  uint8_t cpu_vector = 33;
  uint8_t apic_id = 0;

  // Configure MSI-X in PCI config space (vector 0: queue interrupts)
  pci_configure_msix_vector(platform, bus, slot, func, 0, cpu_vector, apic_id);
  pci_disable_intx(platform, bus, slot, func);
  pci_enable_msix(platform, bus, slot, func);

  // Configure VirtIO device to use MSI-X vectors
  virtio_pci_set_msix_vectors(&platform->virtio_pci_transport_rng, 0xFFFF, 0);
#endif

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

#ifdef __x86_64__
  // x64: Register MSI-X interrupt handler (goes directly to LAPIC, no IOAPIC routing)
  irq_register(platform, cpu_vector, virtio_irq_handler,
               &platform->virtio_rng);
#else
  // Other platforms: Setup interrupt using platform-specific IRQ calculation
  uint8_t irq_pin = platform_pci_config_read8(platform, bus, slot, func,
                                              PCI_REG_INTERRUPT_PIN);
  uint32_t irq_num = platform_pci_irq_swizzle(platform, slot, irq_pin);

  platform_irq_register(platform, irq_num, virtio_irq_handler,
                        &platform->virtio_rng);
  platform_irq_enable(platform, irq_num);
#endif

  // Store pointer to active RNG device
  platform->virtio_rng_ptr = &platform->virtio_rng;
}

// Setup VirtIO-RNG device via MMIO
static void virtio_rng_mmio_setup(platform_t *platform, uint64_t mmio_base,
                                  uint64_t mmio_size, uint32_t irq_num) {
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
  // Assign BAR addresses (bare-metal platforms need to do this manually)
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

#ifdef __x86_64__
  // x64: Use MSI-X interrupts
  uint8_t cpu_vector = 34;
  uint8_t apic_id = 0;

  // Configure MSI-X in PCI config space (vector 0: queue interrupts)
  pci_configure_msix_vector(platform, bus, slot, func, 0, cpu_vector, apic_id);
  pci_disable_intx(platform, bus, slot, func);
  pci_enable_msix(platform, bus, slot, func);

  // Configure VirtIO device to use MSI-X vectors
  virtio_pci_set_msix_vectors(&platform->virtio_pci_transport_blk, 0xFFFF, 0);
#endif

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

#ifdef __x86_64__
  // x64: Register MSI-X interrupt handler (goes directly to LAPIC, no IOAPIC routing)
  irq_register(platform, cpu_vector, virtio_irq_handler,
               &platform->virtio_blk);
#else
  // Other platforms: Setup interrupt using platform-specific IRQ calculation
  uint8_t irq_pin = platform_pci_config_read8(platform, bus, slot, func,
                                              PCI_REG_INTERRUPT_PIN);
  uint32_t irq_num = platform_pci_irq_swizzle(platform, slot, irq_pin);

  platform_irq_register(platform, irq_num, virtio_irq_handler,
                        &platform->virtio_blk);
  platform_irq_enable(platform, irq_num);
#endif

  // Store device info
  platform->virtio_blk_ptr = &platform->virtio_blk;
  platform->has_block_device = true;
  platform->block_sector_size = platform->virtio_blk.sector_size;
  platform->block_capacity = platform->virtio_blk.capacity;

  // Log device info
  uint64_t mb =
      (platform->block_capacity * platform->block_sector_size) / (1024 * 1024);
  KLOG("  sector_size=%u capacity=%llu sectors (%llu MB)",
       platform->block_sector_size,
       (unsigned long long)platform->block_capacity, (unsigned long long)mb);
}

// Setup VirtIO-BLK device via MMIO
static void virtio_blk_mmio_setup(platform_t *platform, uint64_t mmio_base,
                                  uint64_t mmio_size, uint32_t irq_num) {
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
  uint64_t mb =
      (platform->block_capacity * platform->block_sector_size) / (1024 * 1024);
  KLOG("  sector_size=%u capacity=%llu sectors (%llu MB)",
       platform->block_sector_size,
       (unsigned long long)platform->block_capacity, (unsigned long long)mb);
}

// Setup VirtIO-NET device via PCI
static void virtio_net_setup(platform_t *platform, uint8_t bus, uint8_t slot,
                             uint8_t func) {
  // Assign BAR addresses (bare-metal platforms need to do this manually)
  allocate_pci_bars(platform, bus, slot, func, "NET");

  // Re-enable device
  uint16_t command =
      platform_pci_config_read16(platform, bus, slot, func, PCI_REG_COMMAND);
  command |= 0x0006; // Memory space + Bus master
  platform_pci_config_write16(platform, bus, slot, func, PCI_REG_COMMAND,
                              command);

  KDEBUG_LOG("[NET] Initializing PCI transport...");
  // Initialize PCI transport for NET
  if (virtio_pci_init(&platform->virtio_pci_transport_net, platform, bus, slot,
                      func) < 0) {
    KDEBUG_LOG("[NET] PCI transport init failed");
    return;
  }

#ifdef __x86_64__
  // x64: Use MSI-X interrupts
  uint8_t cpu_vector = 35;
  uint8_t apic_id = 0;

  // Configure MSI-X in PCI config space
  // Vector 0: RX queue, Vector 1: TX queue (both use same CPU vector)
  pci_configure_msix_vector(platform, bus, slot, func, 0, cpu_vector, apic_id);
  pci_configure_msix_vector(platform, bus, slot, func, 1, cpu_vector, apic_id);
  pci_disable_intx(platform, bus, slot, func);
  pci_enable_msix(platform, bus, slot, func);

  // Configure VirtIO device to use MSI-X vectors
  virtio_pci_set_msix_vectors(&platform->virtio_pci_transport_net, 0xFFFF, 0);
#endif

  KDEBUG_LOG("[NET] Initializing NET device...");
  // Initialize NET device
  if (virtio_net_init_pci(
          &platform->virtio_net, &platform->virtio_pci_transport_net,
          &platform->virtqueue_net_rx_memory,
          &platform->virtqueue_net_tx_memory, platform->kernel) < 0) {
    KDEBUG_LOG("[NET] NET device init failed");
    return;
  }

  KDEBUG_LOG("[NET] Device initialized successfully");

  // Initialize device base fields
  platform->virtio_net.base.device_type = KDEVICE_TYPE_VIRTIO_NET;
  platform->virtio_net.base.platform = platform;
  platform->virtio_net.base.process_irq = virtio_net_process_irq_dispatch;
  platform->virtio_net.base.ack_isr = virtio_net_ack_isr;

#ifdef __x86_64__
  // x64: Register MSI-X interrupt handler (goes directly to LAPIC, no IOAPIC routing)
  irq_register(platform, cpu_vector, virtio_irq_handler,
               &platform->virtio_net);
#else
  // Other platforms: Setup interrupt using platform-specific IRQ calculation
  uint8_t irq_pin = platform_pci_config_read8(platform, bus, slot, func,
                                              PCI_REG_INTERRUPT_PIN);
  uint32_t irq_num = platform_pci_irq_swizzle(platform, slot, irq_pin);

  platform_irq_register(platform, irq_num, virtio_irq_handler,
                        &platform->virtio_net);
  platform_irq_enable(platform, irq_num);
#endif

  // Store device info
  platform->virtio_net_ptr = &platform->virtio_net;
  platform->has_net_device = true;
  memcpy(platform->net_mac_address, platform->virtio_net.mac_address, 6);

  // Log device info
  KLOG("  mac=%02x:%02x:%02x:%02x:%02x:%02x", platform->net_mac_address[0],
       platform->net_mac_address[1], platform->net_mac_address[2],
       platform->net_mac_address[3], platform->net_mac_address[4],
       platform->net_mac_address[5]);
}

// Setup VirtIO-NET device via MMIO
static void virtio_net_mmio_setup(platform_t *platform, uint64_t mmio_base,
                                  uint64_t mmio_size, uint32_t irq_num) {
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
  memcpy(platform->net_mac_address, platform->virtio_net.mac_address, 6);

  // Log device info
  KLOG("  mac=%02x:%02x:%02x:%02x:%02x:%02x", platform->net_mac_address[0],
       platform->net_mac_address[1], platform->net_mac_address[2],
       platform->net_mac_address[3], platform->net_mac_address[4],
       platform->net_mac_address[5]);
}

// Helper to get device type name
static const char *virtio_device_name(uint16_t device_id) {
  switch (device_id) {
  case VIRTIO_ID_NET:  // MMIO device ID
  case VIRTIO_PCI_DEVICE_NET_LEGACY:
  case VIRTIO_PCI_DEVICE_NET_MODERN:
    return "VirtIO-Net";
  case VIRTIO_ID_BLOCK:  // MMIO device ID
  case VIRTIO_PCI_DEVICE_BLOCK_LEGACY:
  case VIRTIO_PCI_DEVICE_BLOCK_MODERN:
    return "VirtIO-Block";
  case VIRTIO_ID_RNG:  // MMIO device ID
  case VIRTIO_PCI_DEVICE_RNG_LEGACY:
  case VIRTIO_PCI_DEVICE_RNG_MODERN:
    return "VirtIO-RNG";
  default:
    return "VirtIO-Unknown";
  }
}

// Scan PCI bus for VirtIO devices
void pci_scan_devices(platform_t *platform) {
  KDEBUG_LOG("Scanning PCI bus for VirtIO devices...");

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
          KLOG("Found %s at PCI %u:%u.0 (device ID 0x%04x)",
               virtio_device_name(device), bus, slot, device);

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
    KDEBUG_LOG("No VirtIO PCI devices found");
  } else {
    KLOG("Found %d VirtIO PCI device(s) total", devices_found);
  }
}

// Process deferred interrupt work (called from ktick before callbacks)
// External debug counters (x64 only)
#ifdef __x86_64__
extern volatile uint32_t g_msix_irq_count;
#endif

void platform_tick(platform_t *platform, kernel_t *k) {
#ifdef __x86_64__
  // Log MSI-X interrupt count for debugging (x64 only)
  static uint32_t last_msix_count = 0;
  if (g_msix_irq_count != last_msix_count) {
    printk("[DEBUG] MSI-X interrupts: ");
    printk_dec(g_msix_irq_count);
    printk("\n");
    last_msix_count = g_msix_irq_count;
  }

  // Log ALL interrupt count for debugging MMIO (x64 only)
  extern volatile uint32_t g_irq_count;
  extern volatile uint32_t g_last_irq_vector;
  static uint32_t last_irq_count = 0;
  if (g_irq_count != last_irq_count) {
    printk("[DEBUG] Total IRQs: ");
    printk_dec(g_irq_count);
    printk(" (last vector: ");
    printk_dec(g_last_irq_vector);
    printk(")\n");
    last_irq_count = g_irq_count;
  }
#endif

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
#ifdef __x86_64__
// x64-specific device name helper (for ACPI-discovered devices)
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
#endif

// Probe for VirtIO MMIO devices at known addresses
// Platform must populate virtio_mmio_base in platform_t from FDT
void mmio_scan_devices(platform_t *platform) {
  KDEBUG_LOG("Probing for VirtIO MMIO devices...");

#ifdef __x86_64__
  // x64: Use ACPI DSDT to discover virtio-mmio devices
  acpi_virtio_mmio_device_t acpi_devices[MAX_VIRTIO_MMIO_DEVICES];
  int acpi_device_count = acpi_find_virtio_mmio_devices(
      platform, acpi_devices, MAX_VIRTIO_MMIO_DEVICES);

  int rng_initialized = 0;
  int blk_initialized = 0;
  int net_initialized = 0;

  // Process each ACPI-discovered device
  for (int i = 0; i < acpi_device_count; i++) {
    if (!acpi_devices[i].valid) {
      continue;
    }

    uint64_t base = acpi_devices[i].mmio_base;
    uint32_t irq_num = acpi_devices[i].irq;

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

    KLOG("Found %s at MMIO 0x%llx (device ID %u)",
         virtio_mmio_device_name(device_id), (unsigned long long)base,
         device_id);

    // Initialize RNG device if found and not already initialized
    if (device_id == VIRTIO_ID_RNG && !rng_initialized &&
        platform->virtio_rng_ptr == NULL) {
      virtio_rng_mmio_setup(platform, base, acpi_devices[i].mmio_size, irq_num);
      rng_initialized = 1;
    }

    // Initialize BLK device if found and not already initialized
    if (device_id == VIRTIO_ID_BLOCK && !blk_initialized &&
        platform->virtio_blk_ptr == NULL) {
      virtio_blk_mmio_setup(platform, base, acpi_devices[i].mmio_size, irq_num);
      blk_initialized = 1;
    }

    // Initialize NET device if found and not already initialized
    if (device_id == VIRTIO_ID_NET && !net_initialized &&
        platform->virtio_net_ptr == NULL) {
      virtio_net_mmio_setup(platform, base, acpi_devices[i].mmio_size, irq_num);
      net_initialized = 1;
    }
  }

  KLOG("Initialized %d virtio-mmio device(s) from ACPI", acpi_device_count);
#else
  // Non-x64 platforms: Use hardcoded MMIO base and IRQ mapping
  uint64_t mmio_base = platform->virtio_mmio_base;
  if (mmio_base == 0) {
    mmio_base = VIRTIO_MMIO_BASE;
  }

  int devices_found = 0;
  int rng_initialized = 0;
  int blk_initialized = 0;
  int net_initialized = 0;

  // Scan for devices
  for (int i = 0; i < VIRTIO_MMIO_MAX_DEVICES; i++) {
    uint64_t base = mmio_base + (i * VIRTIO_MMIO_DEVICE_STRIDE);

    // Read magic value
    volatile uint32_t *magic_ptr = (volatile uint32_t *)base;
    uint32_t magic = *magic_ptr;

    if (magic != VIRTIO_MMIO_MAGIC) {
      continue; // No device at this address
    }

    // Read device ID
    volatile uint32_t *device_id_ptr =
        (volatile uint32_t *)(base + VIRTIO_MMIO_DEVICE_ID);
    uint32_t device_id = *device_id_ptr;

    if (device_id == 0) {
      continue; // No device
    }

    KLOG("Found %s at MMIO 0x%llx (device ID %d)", virtio_device_name(device_id),
         (unsigned long long)base, device_id);
    devices_found++;

    // Initialize RNG device if found and not already initialized
    if (device_id == VIRTIO_ID_RNG && !rng_initialized &&
        platform->virtio_rng_ptr == NULL) {
      uint32_t irq_num = platform_mmio_irq_number(platform, i);
      virtio_rng_mmio_setup(platform, base, VIRTIO_MMIO_DEVICE_STRIDE, irq_num);
      rng_initialized = 1;
    }

    // Initialize BLK device if found and not already initialized
    if (device_id == VIRTIO_ID_BLOCK && !blk_initialized &&
        platform->virtio_blk_ptr == NULL) {
      uint32_t irq_num = platform_mmio_irq_number(platform, i);
      virtio_blk_mmio_setup(platform, base, VIRTIO_MMIO_DEVICE_STRIDE, irq_num);
      blk_initialized = 1;
    }

    // Initialize NET device if found and not already initialized
    if (device_id == VIRTIO_ID_NET && !net_initialized &&
        platform->virtio_net_ptr == NULL) {
      uint32_t irq_num = platform_mmio_irq_number(platform, i);
      virtio_net_mmio_setup(platform, base, VIRTIO_MMIO_DEVICE_STRIDE, irq_num);
      net_initialized = 1;
    }
  }

  if (devices_found == 0) {
    KDEBUG_LOG("No VirtIO MMIO devices found");
  } else {
    KLOG("Found %d VirtIO MMIO device(s) total", devices_found);
  }
#endif
}

// rv32 Platform Initialization
// Sets up interrupts, timer, and device enumeration

#include "interrupt.h"
#include "mem_debug.h"
#include "platform.h"
#include "platform_impl.h"
#include "printk.h"
#include "sbi.h"
#include "timer.h"
#include "virtio/virtio_rng.h"
#include <stddef.h>

// PCI config space register offsets
#define PCI_REG_INTERRUPT_PIN 0x3D

// Forward declarations
extern void pci_scan_devices(platform_t *platform);
extern void mmio_scan_devices(platform_t *platform);

static void wfi_timer_callback(void) {}

// Platform-specific initialization
void platform_init(platform_t *platform, void *fdt, void *kernel) {
  platform->kernel = kernel;
  platform->virtio_rng_ptr = NULL;
  platform->virtio_blk_ptr = NULL;
  platform->virtio_net_ptr = NULL;
  platform->has_block_device = false;
  platform->has_net_device = false;
  platform->timer_freq = 0;
  platform->ticks_per_ms = 0;
  platform->last_overflow_count = 0;

  // Initialize PCI BAR allocator
  // RISC-V QEMU virt PCI MMIO region starts at 0x40000000
  platform->pci_next_bar_addr = 0x40000000;

  // Initialize IRQ ring buffer
  kirq_ring_init(&platform->irq_ring);

  printk("Initializing rv32 platform...\n");

  // Initialize interrupt handling (trap vector)
  interrupt_init(platform);

  // Initialize timer (reads frequency from FDT)
  timer_init(platform, fdt);

  // For typical timer frequencies (< 4GHz), this fits in 32-bit division
  if (platform->timer_freq < 0x100000000ULL) {
    platform->ticks_per_ms = (uint32_t)platform->timer_freq / 1000;
  } else {
    // Shouldn't happen in practice, but handle gracefully
    platform->ticks_per_ms = 10000; // Fallback: assume 10MHz
  }

  printk("Timer frequency: ");
  printk_dec((uint32_t)platform->timer_freq);
  printk(" Hz\n");

  printk("Ticks per ms: ");
  printk_dec((uint32_t)platform->ticks_per_ms);
  printk("\n");

  // NOTE: Interrupts NOT enabled yet - will be enabled in event loop
  // to avoid spurious interrupts during device enumeration

  // Parse and display device tree
  platform_fdt_dump(platform, fdt);

  // Scan for VirtIO devices via both PCI and MMIO
  printk("=== Starting VirtIO Device Scan ===\n\n");
  pci_scan_devices(platform);
  mmio_scan_devices(platform);

  printk("\nPlatform initialization complete.\n\n");
}

// Wait for interrupt with timeout
// timeout_ms: timeout in milliseconds (UINT64_MAX = wait forever)
// Returns: current time in milliseconds
uint64_t platform_wfi(platform_t *platform, uint64_t timeout_ms) {
  if (timeout_ms == 0) {
    return timer_get_current_time_ms(platform);
  }

  // Disable interrupts to check condition atomically
  __asm__ volatile("csrci sstatus, 0x2" ::: "memory");

  // Check if an interrupt has already fired (ring buffer has pending work)
  if (!kirq_ring_is_empty(&platform->irq_ring)) {
    platform_interrupt_enable(platform);
    return timer_get_current_time_ms(platform);
  }

  // Set timeout timer if not UINT64_MAX
  if (timeout_ms != UINT64_MAX) {
    // For timeouts > UINT32_MAX ms, cap at UINT32_MAX
    uint32_t timeout_ms_32 =
        (timeout_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)timeout_ms;
    // Set callback before calling timer_set_oneshot_ms
    platform->timer_callback = wfi_timer_callback;
    timer_set_oneshot_ms(platform, timeout_ms_32);
  }

  // Atomically enable interrupts and wait
  __asm__ volatile("wfi" ::: "memory");
  __asm__ volatile("csrsi sstatus, 0x2" ::: "memory");

  // Cancel timer if it was set
  if (timeout_ms != UINT64_MAX) {
    timer_cancel(platform);
  }

  // Return current time
  return timer_get_current_time_ms(platform);
}

// Abort system execution (shutdown/halt)
void platform_abort(void) {
  // Note: platform_interrupt_disable requires platform_t*, but we don't have
  // access here. The CSR manipulation below achieves the same result.
  // Disable interrupts using raw CSR access
  __asm__ volatile("csrc sstatus, %0" ::"r"(1 << 1)); // Clear SIE bit
  // Request shutdown via SBI (causes QEMU to exit cleanly)
  sbi_shutdown();
  // Should never reach here, but halt just in case
  while (1) {
    __asm__ volatile("wfi");
  }
}

// =============================================================================
// Device Discovery (platform.h contract)
// =============================================================================

// Discover MMIO devices via hardcoded probing (platform.h contract)
// Probes known MMIO address range for VirtIO devices and reads their device IDs
int platform_discover_mmio_devices(platform_t *platform,
                                   platform_mmio_device_t *devices,
                                   int max_devices) {
  // Get MMIO base address from platform or use default
  uint64_t mmio_base = platform->virtio_mmio_base;
  if (mmio_base == 0) {
    mmio_base = VIRTIO_MMIO_BASE;
  }

  int valid_count = 0;

  // Scan for devices at known MMIO slots
  for (int i = 0; i < VIRTIO_MMIO_MAX_DEVICES && valid_count < max_devices;
       i++) {
    uint64_t base = mmio_base + (i * VIRTIO_MMIO_DEVICE_STRIDE);

    // Read magic value at offset 0x00
    volatile uint32_t *magic_ptr = (volatile uint32_t *)base;
    uint32_t magic = *magic_ptr;

    // VirtIO magic value is 0x74726976 ("virt" in little-endian)
    if (magic != VIRTIO_MMIO_MAGIC) {
      continue; // No device at this address
    }

    // Read device ID at offset 0x08
    volatile uint32_t *device_id_ptr =
        (volatile uint32_t *)(base + VIRTIO_MMIO_DEVICE_ID);
    uint32_t device_id = *device_id_ptr;

    // Device ID 0 means empty slot
    if (device_id == 0) {
      continue;
    }

    // Calculate IRQ number using platform-specific function
    uint32_t irq_num = platform_mmio_irq_number(platform, i);

    // Fill platform-independent device descriptor
    devices[valid_count].mmio_base = base;
    devices[valid_count].mmio_size = VIRTIO_MMIO_DEVICE_STRIDE;
    devices[valid_count].irq_num = irq_num;
    devices[valid_count].device_id = device_id;
    devices[valid_count].valid = true;
    valid_count++;
  }

  return valid_count;
}

// Configure PCI device interrupts (RISC-V 32: legacy INTx)
// Returns hardware IRQ number to use for interrupt registration
int platform_pci_setup_interrupts(platform_t *platform, uint8_t bus,
                                  uint8_t slot, uint8_t func,
                                  void *transport) {
  (void)bus;       // Unused on RISC-V
  (void)transport; // Not needed for INTx

  // Read PCI interrupt pin from config space
  uint8_t irq_pin = platform_pci_config_read8(platform, bus, slot, func,
                                              PCI_REG_INTERRUPT_PIN);

  // Calculate hardware IRQ number using platform swizzling
  uint32_t irq_num = platform_pci_irq_swizzle(platform, slot, irq_pin);

  return (int)irq_num;
}

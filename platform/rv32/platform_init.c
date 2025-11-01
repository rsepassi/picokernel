// rv32 Platform Initialization
// Sets up interrupts, timer, and device enumeration

#include "interrupt.h"
#include "platform.h"
#include "printk.h"
#include "sbi.h"
#include "timer.h"
#include "virtio/virtio_rng.h"
#include <stddef.h>

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

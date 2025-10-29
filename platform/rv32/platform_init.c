// rv32 Platform Initialization
// Sets up interrupts, timer, and device enumeration

#include "interrupt.h"
#include "platform.h"
#include "printk.h"
#include "sbi.h"
#include "timer.h"
#include "virtio/virtio_rng.h"
#include <stddef.h>

// Forward declare device tree dump function
void platform_fdt_dump(void *fdt);

// Forward declarations
extern void pci_scan_devices(platform_t *platform);
extern void mmio_scan_devices(platform_t *platform);

static void wfi_timer_callback(void) {}

// Platform-specific initialization
void platform_init(platform_t *platform, void *fdt) {
  platform->virtio_rng = NULL;
  platform->timer_freq = 0;
  platform->ticks_per_ms = 0;

  printk("Initializing rv32 platform...\n");

  // Initialize interrupt handling (trap vector)
  interrupt_init();

  // Initialize timer (reads frequency from FDT)
  timer_init(fdt);

  // Store timer frequency in platform state
  platform->timer_freq = timer_get_frequency();
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
  platform_fdt_dump(fdt);

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
    return timer_get_current_time_ms();
  }

  // Disable interrupts to check condition atomically
  platform_interrupt_disable();

  // Check if an interrupt has already fired
  virtio_rng_dev_t *rng = platform->virtio_rng;
  if (rng != NULL && rng->irq_pending) {
    platform_interrupt_enable();
    return timer_get_current_time_ms();
  }

  // Set timeout timer if not UINT64_MAX
  if (timeout_ms != UINT64_MAX) {
    // For timeouts > UINT32_MAX ms, cap at UINT32_MAX
    uint32_t timeout_ms_32 =
        (timeout_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)timeout_ms;
    timer_set_oneshot_ms(timeout_ms_32, wfi_timer_callback);
  }

  // Atomically enable interrupts and wait
  platform_interrupt_enable();
  __asm__ volatile("wfi");

  // Return current time
  return timer_get_current_time_ms();
}

// RISC-V 64-bit Platform Initialization
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
  platform->virtio_rng = NULL;

  printk("Initializing RISC-V 64-bit platform...\n");

  // Initialize interrupt handling (trap vector)
  interrupt_init(platform);

  // Initialize timer and read timebase frequency from FDT
  timer_init(platform, fdt);

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
  platform_interrupt_disable(platform);

  // Check if an interrupt has already fired
  virtio_rng_dev_t *rng = platform->virtio_rng;
  if (rng != NULL && rng->irq_pending) {
    platform_interrupt_enable(platform);
    return timer_get_current_time_ms(platform);
  }

  // Set timeout timer if not UINT64_MAX
  if (timeout_ms != UINT64_MAX) {
    // For timeouts > UINT32_MAX ms, cap at UINT32_MAX
    uint32_t timeout_ms_32 =
        (timeout_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)timeout_ms;
    platform->timer_callback = wfi_timer_callback;
    timer_set_oneshot_ms(platform, timeout_ms_32);
  }

  // Atomically enable interrupts and wait
  // RISC-V doesn't have a single instruction for this, so we enable then wfi
  platform_interrupt_enable(platform);
  __asm__ volatile("wfi" ::: "memory");

  // Return current time
  return timer_get_current_time_ms(platform);
}

// Abort system execution (shutdown/halt)
void platform_abort(void) {
  // Disable interrupts (no platform context available in abort)
  // This is a platform global operation - use NULL to indicate no specific platform
  platform_interrupt_disable(NULL);
  // Request shutdown via SBI (causes QEMU to exit cleanly)
  sbi_shutdown();
  // Should never reach here, but halt just in case
  while (1) {
    __asm__ volatile("wfi" ::: "memory");
  }
}

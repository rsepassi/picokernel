// RISC-V 64-bit Platform Initialization
// Sets up interrupts, timer, and device enumeration

#include "interrupt.h"
#include "platform.h"
#include "printk.h"
#include "sbi.h"
#include "timer.h"
#include "virtio/virtio_rng.h"
#include <stddef.h>

// Forward declare internal device enumeration function
void platform_fdt_dump(void *fdt);

// Forward declarations
extern void pci_scan_devices(platform_t *platform);
extern void mmio_scan_devices(platform_t *platform);

static void wfi_timer_callback(void) {}

// Platform-specific initialization
void platform_init(platform_t *platform, void *fdt) {
  platform->virtio_rng = NULL;
  platform->timebase_freq = 0;

  printk("Initializing RISC-V 64-bit platform...\n");

  // Initialize interrupt handling (trap vector)
  interrupt_init();

  // Initialize timer and read timebase frequency from FDT
  timer_init(fdt, &platform->timebase_freq);

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
  // RISC-V doesn't have a single instruction for this, so we enable then wfi
  platform_interrupt_enable();
  __asm__ volatile("wfi" ::: "memory");

  // Return current time
  return timer_get_current_time_ms();
}

// ARM32 Platform Initialization
// Sets up interrupts, timer, and device enumeration

#include "interrupt.h"
#include "platform.h"
#include "printk.h"
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
  kirq_ring_init(&platform->irq_ring);

  printk("Initializing ARM32 platform...\n");

  // Initialize interrupt handling (GIC and exception vectors)
  interrupt_init(platform);

  // Initialize ARM Generic Timer
  timer_init(platform);

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
  __asm__ volatile("cpsid i" ::: "memory"); // Disable IRQs

  // Check if an interrupt has already fired (ring buffer has pending work)
  if (!kirq_ring_is_empty(&platform->irq_ring)) {
    __asm__ volatile("cpsie i" ::: "memory"); // Re-enable IRQs
    return timer_get_current_time_ms(platform);
  }

  // Set timeout timer if not UINT64_MAX
  if (timeout_ms != UINT64_MAX) {
    // For timeouts > UINT32_MAX ms, cap at UINT32_MAX
    uint32_t timeout_ms_32 =
        (timeout_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)timeout_ms;
    timer_set_oneshot_ms(platform, timeout_ms_32, wfi_timer_callback);
  }

  // Atomically enable interrupts and wait
  __asm__ volatile("cpsie i; wfi" ::: "memory");

  // Cancel timer if it was set
  if (timeout_ms != UINT64_MAX) {
    timer_cancel(platform);
  }

  // Return current time
  return timer_get_current_time_ms(platform);
}

// Abort system execution (shutdown/halt)
void platform_abort(void) {
  // Disable interrupts
  __asm__ volatile("cpsid i" ::: "memory"); // Disable IRQs
  // Trigger undefined instruction exception (causes QEMU to exit)
  // UDF (undefined) instruction
  __asm__ volatile(".word 0xe7f000f0" ::: "memory");
  // Should never reach here, but halt just in case
  while (1) {
    __asm__ volatile("wfi" ::: "memory");
  }
}

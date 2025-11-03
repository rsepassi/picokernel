// ARM64 Platform Initialization
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
extern void platform_mem_init(platform_t *platform);

static void wfi_timer_callback(void) {}

// Platform-specific initialization
void platform_init(platform_t *platform, void *fdt, void *kernel) {
  KLOG("arm64 init...");

  memset(platform, 0, sizeof(platform_t));
  platform->kernel = kernel;

  KLOG("fdt parse...");
  platform_boot_context_parse(platform, fdt);
  KLOG("fdt parse ok");

  KLOG("uart init...");
  platform_uart_init(platform);
  KLOG("uart init ok");

  KLOG("mem init...");
  platform_mem_init(platform);
  KLOG("mem init ok");

  KLOG("interrupt init...");
  interrupt_init(platform);
  KLOG("interrupt init ok");

  KLOG("timer init...");
  timer_init(platform);
  KLOG("timer init ok");

  KDEBUG_VALIDATE(platform_fdt_dump(platform, fdt));

  KLOG("virtio scan...");
  pci_scan_devices(platform);
  mmio_scan_devices(platform);
  KLOG("virtio scan ok");

  KLOG("arm64 init ok");
}

// Wait for interrupt with timeout
// timeout_ms: timeout in milliseconds (UINT64_MAX = wait forever)
// Returns: current time in milliseconds
uint64_t platform_wfi(platform_t *platform, uint64_t timeout_ms) {
  if (timeout_ms == 0) {
    return timer_get_current_time_ms(platform);
  }

  // Disable interrupts to check condition atomically
  __asm__ volatile("msr daifset, #2" ::: "memory"); // Disable IRQs

  // Check if an interrupt has already fired (ring buffer not empty)
  if (!kirq_ring_is_empty(&platform->irq_ring)) {
    __asm__ volatile("msr daifclr, #2" ::: "memory"); // Re-enable IRQs
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
  __asm__ volatile("wfi" ::: "memory");
  __asm__ volatile("msr daifclr, #2" ::: "memory");

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
  __asm__ volatile("msr daifset, #2" ::: "memory"); // Disable IRQs
  // Trigger undefined instruction exception (causes QEMU to exit)
  // Using .word directive for undefined instruction
  __asm__ volatile(".word 0x00000000" ::: "memory");
  // Should never reach here, but halt just in case
  while (1) {
    __asm__ volatile("wfi" ::: "memory");
  }
}

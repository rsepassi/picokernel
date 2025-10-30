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

static void wfi_timer_callback(void) {}

// Platform-specific initialization
void platform_init(platform_t *platform, void *fdt, void *kernel) {
  platform->kernel = kernel;
  platform->virtio_rng_ptr = NULL;
  platform->virtio_blk_ptr = NULL;
  platform->has_block_device = false;

  // Initialize PCI BAR allocator (QEMU ARM64 virt: PCI MMIO at 0x10000000)
  platform->pci_next_bar_addr = 0x10000000;

  // Initialize IRQ ring buffer
  kirq_ring_init(&platform->irq_ring);
  platform->last_overflow_count = 0;

  printk("Initializing ARM64 platform...\n");

  // Initialize exception vectors and GIC
  interrupt_init(platform);

  // Initialize ARM Generic Timer
  timer_init(platform);

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

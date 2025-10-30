// x32 Platform Initialization
// Sets up interrupts, timer, and device enumeration

#include "acpi.h"
#include "interrupt.h"
#include "platform.h"
#include "printk.h"
#include "timer.h"
#include "virtio/virtio_rng.h"
#include <stddef.h>

// Forward declare device scanning functions
void pci_scan_devices(platform_t *platform);
void mmio_scan_devices(platform_t *platform);

// Track WFI wake status
static volatile int g_wfi_done = 0;

// Timer callback function - wake from WFI
static void wfi_timer_callback(void) { g_wfi_done = 1; }

// Platform-specific initialization
void platform_init(platform_t *platform, void *fdt, void *kernel) {
  (void)fdt; // x32 doesn't use FDT, but keep parameter for consistency

  platform->kernel = kernel;
  platform->virtio_rng_ptr = NULL;

  printk("Initializing x32 platform...\n");

  // Initialize ACPI (must come before interrupt init, which uses ACPI for
  // IOAPIC)
  acpi_init(platform);

  // Initialize interrupt handling (IDT)
  interrupt_init(platform);

  // Initialize Local APIC timer
  timer_init(platform);

  // NOTE: Interrupts NOT enabled yet - will be enabled in event loop
  // to avoid spurious interrupts during device enumeration

  printk("\n");

  // Parse and display device tree (ACPI-based on x32)
  platform_fdt_dump(platform, NULL);

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

  // Disable interrupts atomically
  __asm__ volatile("cli");

  // Check if interrupt already pending
  virtio_rng_dev_t *rng = platform->virtio_rng_ptr;
  if (rng != NULL && rng->irq_pending) {
    __asm__ volatile("sti");
    return timer_get_current_time_ms(platform);
  }

  // Set timeout timer if not UINT64_MAX
  if (timeout_ms != UINT64_MAX) {
    uint32_t timeout_ms_32 =
        (timeout_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)timeout_ms;
    timer_set_oneshot_ms(platform, timeout_ms_32, wfi_timer_callback);
  }

  // Atomically enable interrupts and wait
  __asm__ volatile("sti; hlt");

  return timer_get_current_time_ms(platform);
}

// Abort system execution (shutdown/halt)
void platform_abort(void) {
  // Disable interrupts
  __asm__ volatile("cli");
  // Trigger undefined opcode exception (causes QEMU to exit)
  __asm__ volatile("ud2");
  // Should never reach here, but halt just in case
  while (1) {
    __asm__ volatile("hlt");
  }
}

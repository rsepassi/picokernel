// x32 Platform Initialization
// Sets up interrupts, timer, and device enumeration

#include "interrupt.h"
#include "platform.h"
#include "printk.h"
#include "timer.h"
#include <stddef.h>

// Forward declare internal device enumeration function
void fdt_dump(void *fdt);

// Global platform state for interrupt tracking
static platform_t *g_platform = NULL;

// Track last interrupt type
static volatile uint32_t g_last_interrupt = PLATFORM_INT_UNKNOWN;
static volatile int g_wfi_done = 0;

// Timer callback function - record timeout and wake from WFI
static void wfi_timer_callback(void) {
  g_last_interrupt = PLATFORM_INT_TIMEOUT;
  g_wfi_done = 1;
}

// Platform-specific initialization
void platform_init(platform_t *platform, void *fdt) {
  (void)fdt; // x32 doesn't use FDT, but keep parameter for consistency

  g_platform = platform;
  platform->last_interrupt = PLATFORM_INT_UNKNOWN;

  printk("Initializing x32 platform...\n");

  // Initialize interrupt handling (IDT)
  interrupt_init();

  // Initialize Local APIC timer
  timer_init();

  // Enable interrupts globally
  interrupt_enable();

  printk("Interrupts enabled.\n\n");

  // Parse and display device tree (ACPI-based on x32)
  fdt_dump(NULL);

  printk("Platform initialization complete.\n\n");
}

// Wait for interrupt with timeout
// timeout_ms: timeout in milliseconds (UINT64_MAX = wait forever)
// Returns: reason code indicating what interrupt fired
uint32_t platform_wfi(platform_t *platform, uint64_t timeout_ms) {
  g_last_interrupt = PLATFORM_INT_UNKNOWN;
  g_wfi_done = 0;

  // Set timeout timer if not UINT64_MAX
  if (timeout_ms != UINT64_MAX) {
    // For timeouts > UINT32_MAX ms, cap at UINT32_MAX
    uint32_t timeout_ms_32 =
        (timeout_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)timeout_ms;
    timer_set_oneshot_ms(timeout_ms_32, wfi_timer_callback);
  }

  // Wait for interrupt
  while (!g_wfi_done) {
    __asm__ volatile("hlt");
  }

  // Update platform state
  platform->last_interrupt = g_last_interrupt;

  return g_last_interrupt;
}

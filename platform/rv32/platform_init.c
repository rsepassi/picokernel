// rv32 Platform Initialization
// Sets up interrupts, timer, and device enumeration

#include "interrupt.h"
#include "platform.h"
#include "printk.h"
#include "sbi.h"
#include "timer.h"
#include <stddef.h>

// Forward declare device tree dump function
void platform_fdt_dump(void *fdt);

// Global platform state
static platform_t *g_platform = NULL;

// Track last interrupt type and wfi completion
static volatile uint32_t g_last_interrupt = PLATFORM_INT_UNKNOWN;
static volatile int g_wfi_done = 0;

// Timer callback function - record timeout and wake from WFI
static void wfi_timer_callback(void) {
  g_last_interrupt = PLATFORM_INT_TIMEOUT;
  g_wfi_done = 1;
}

// Platform-specific initialization
void platform_init(platform_t *platform, void *fdt) {
  g_platform = platform;
  platform->last_interrupt = PLATFORM_INT_UNKNOWN;
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

  // Enable interrupts globally
  interrupt_enable();

  printk("Interrupts enabled.\n\n");

  // Parse and display device tree
  platform_fdt_dump(fdt);

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
  // The WFI instruction will put the CPU in low-power mode until an interrupt
  // occurs
  while (!g_wfi_done) {
    asm volatile("wfi");
  }

  // Update platform state
  platform->last_interrupt = g_last_interrupt;

  return g_last_interrupt;
}

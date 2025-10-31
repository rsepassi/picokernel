// ARM32 Generic Timer Driver
// Uses ARM Generic Timer (ARMv7-A) for one-shot timers

#include "timer.h"
#include "platform_impl.h"
#include "printk.h"
#include <stddef.h>
#include <stdint.h>

// ARM Generic Timer registers (CP15 coprocessor)
// CNTFRQ: Counter Frequency register
// CNTV_CTL: Virtual Timer Control register
// CNTV_TVAL: Virtual Timer TimerValue register

// Timer control register bits
#define TIMER_ENABLE (1 << 0)  // Enable timer
#define TIMER_IMASK (1 << 1)   // Interrupt mask (0 = not masked)
#define TIMER_ISTATUS (1 << 2) // Interrupt status (read-only)

// Read CNTFRQ (Counter Frequency) register
static inline uint32_t read_cntfrq(void) {
  uint32_t value;
  __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(value));
  return value;
}

// Read CNTVCT (Virtual Counter) register - 64-bit
static inline uint64_t read_cntvct(void) {
  uint32_t low, high;
  __asm__ volatile("mrrc p15, 1, %0, %1, c14" : "=r"(low), "=r"(high));
  return ((uint64_t)high << 32) | low;
}

// Write CNTV_TVAL (Virtual Timer TimerValue) register - 32-bit
static inline void write_cntv_tval(uint32_t value) {
  __asm__ volatile("mcr p15, 0, %0, c14, c3, 0" : : "r"(value));
}

// Write CNTV_CTL (Virtual Timer Control) register
static inline void write_cntv_ctl(uint32_t value) {
  __asm__ volatile("mcr p15, 0, %0, c14, c3, 1" : : "r"(value));
}

// Timer interrupt handler (called from interrupt.c via IRQ dispatch)
void generic_timer_handler(void *context) {
  platform_t *platform = (platform_t *)context;

  // Disable the timer to prevent further interrupts
  write_cntv_ctl(0);

  // Call the user's callback if set
  if (platform->timer_callback) {
    timer_callback_t cb = platform->timer_callback;
    platform->timer_callback = NULL; // Clear before calling
    cb();
  }
}

// Initialize ARM Generic Timer
void timer_init(platform_t *platform) {
  // Read timer frequency from CNTFRQ register
  platform->timer_freq_hz = read_cntfrq();

  if (platform->timer_freq_hz == 0) {
    printk("WARNING: Timer frequency is 0, using default 62.5 MHz\n");
    platform->timer_freq_hz = 62500000; // Common default for QEMU
  }

  printk("ARM Generic Timer initialized (virtual timer)\n");
  printk("Timer frequency: ");
  printk_dec(platform->timer_freq_hz);
  printk(" Hz (");
  printk_dec(platform->timer_freq_hz / 1000000);
  printk(" MHz)\n");

  // Disable timer initially
  write_cntv_ctl(0);

  // Capture start time for timer_get_current_time_ms()
  platform->timer_start = read_cntvct();
}

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(platform_t *platform, uint32_t milliseconds,
                          timer_callback_t callback) {
  if (callback == NULL) {
    printk("timer_set_oneshot_ms: NULL callback\n");
    return;
  }

  if (platform->timer_freq_hz == 0) {
    printk("timer_set_oneshot_ms: Timer not initialized\n");
    return;
  }

  platform->timer_callback = callback;

  // Calculate tick count
  // ticks = (milliseconds * freq_hz) / 1000
  // Use 64-bit arithmetic to avoid overflow
  uint64_t ticks = ((uint64_t)milliseconds * platform->timer_freq_hz) / 1000;

  // Ensure we have at least 1 tick
  if (ticks == 0) {
    ticks = 1;
  }

  printk("Timer set for ");
  printk_dec(milliseconds);
  printk("ms (");
  printk_dec((uint32_t)ticks);
  printk(" ticks)\n");

  // Disable timer first
  write_cntv_ctl(0);

  // Set the timer value (counts down from this value)
  write_cntv_tval((uint32_t)ticks);

  // Enable timer with interrupts unmasked
  // Bit 0 = Enable, Bit 1 = 0 (unmask interrupt)
  write_cntv_ctl(TIMER_ENABLE);
}

// Get the timer frequency in Hz
uint64_t timer_get_frequency(platform_t *platform) {
  return platform->timer_freq_hz;
}

// Get current time in milliseconds
uint64_t timer_get_current_time_ms(platform_t *platform) {
  uint64_t counter_now = read_cntvct();
  uint64_t counter_elapsed = counter_now - platform->timer_start;

  if (platform->timer_freq_hz == 0) {
    return 0;
  }

  // Convert counter ticks to milliseconds
  // ms = (ticks * 1000) / freq_hz
  return (counter_elapsed * 1000) / platform->timer_freq_hz;
}

// Cancel any pending timer
void timer_cancel(platform_t *platform) {
  (void)platform;
  write_cntv_ctl(0);  // Disable timer
}

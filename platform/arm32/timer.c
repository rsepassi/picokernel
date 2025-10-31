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

// Timer interrupt handler (called from interrupt.c)
void timer_handler(platform_t *platform) {
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
  platform->timer_freq = read_cntfrq();

  if (platform->timer_freq == 0) {
    printk("ERROR: Timer frequency is 0 Hz\n");
    printk("Generic Timer may not be supported on this system\n");
    return;
  }

  // Calculate ticks per millisecond
  platform->ticks_per_ms = platform->timer_freq / 1000;

  printk("ARM Generic Timer initialized (virtual timer)\n");
  printk("Timer frequency: ");
  printk_dec(platform->timer_freq);
  printk(" Hz (");
  printk_dec(platform->ticks_per_ms);
  printk(" ticks/ms)\n");

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

  if (platform->timer_freq == 0) {
    printk("timer_set_oneshot_ms: Timer not initialized\n");
    return;
  }

  platform->timer_callback = callback;

  // Calculate tick count
  uint32_t ticks = milliseconds * platform->ticks_per_ms;

  // Disable timer first
  write_cntv_ctl(0);

  // Set the timer value (counts down from this value)
  write_cntv_tval(ticks);

  // Enable timer with interrupts unmasked
  // Bit 0 = Enable, Bit 1 = 0 (unmask interrupt)
  write_cntv_ctl(TIMER_ENABLE);

  printk("Timer set for ");
  printk_dec(milliseconds);
  printk("ms (");
  printk_dec(ticks);
  printk(" ticks)\n");
}

// Get the timer frequency in Hz
uint32_t timer_get_frequency(platform_t *platform) {
  return platform->timer_freq;
}

// Get current time in milliseconds
uint64_t timer_get_current_time_ms(platform_t *platform) {
  uint64_t counter_now = read_cntvct();
  uint64_t counter_elapsed = counter_now - platform->timer_start;

  if (platform->timer_freq == 0) {
    return 0;
  }

  // Convert counter ticks to milliseconds
  // ms = (ticks * 1000) / freq_hz
  return (counter_elapsed * 1000) / platform->timer_freq;
}

// Cancel any pending timer
void timer_cancel(platform_t *platform) {
  (void)platform;
  write_cntv_ctl(0);  // Disable timer
}

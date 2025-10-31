// ARM64 Generic Timer Driver
// Uses the ARM architected Generic Timer (EL1 Physical Timer)

#include "timer.h"
#include "platform_impl.h"
#include "printk.h"

// Read system register helpers
static inline uint64_t read_cntfrq_el0(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
  return val;
}

static inline uint64_t read_cntpct_el0(void) {
  uint64_t val;
  __asm__ volatile("mrs %0, cntpct_el0" : "=r"(val));
  return val;
}

static inline void write_cntp_ctl_el0(uint64_t val) {
  __asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(val));
  __asm__ volatile("isb");
}

static inline void write_cntp_tval_el0(uint64_t val) {
  __asm__ volatile("msr cntp_tval_el0, %0" : : "r"(val));
  __asm__ volatile("isb");
}

// Timer interrupt handler (called from interrupt.c via IRQ dispatch)
void generic_timer_handler(void *context) {
  platform_t *platform = (platform_t *)context;

  // Disable timer to prevent further interrupts
  // Clear ENABLE bit (bit 0) and IMASK bit (bit 1)
  write_cntp_ctl_el0(0);

  // Call the user's callback if set
  if (platform->timer_callback) {
    timer_callback_t cb = platform->timer_callback;
    platform->timer_callback = NULL; // Clear before calling
    cb();
  }
}

// Initialize Generic Timer
void timer_init(platform_t *platform) {
  // Read timer frequency from CNTFRQ_EL0
  // This is set by firmware/bootloader and is architected
  platform->timer_freq_hz = read_cntfrq_el0();

  if (platform->timer_freq_hz == 0) {
    printk("WARNING: Timer frequency is 0, using default 62.5 MHz\n");
    platform->timer_freq_hz = 62500000; // Common default for QEMU
  }

  printk("ARM Generic Timer initialized\n");
  printk("Timer frequency: ");
  printk_dec(platform->timer_freq_hz);
  printk(" Hz (");
  printk_dec(platform->timer_freq_hz / 1000000);
  printk(" MHz)\n");

  // Disable timer initially
  write_cntp_ctl_el0(0);

  // Capture start time for timer_get_current_time_ms()
  platform->timer_start = read_cntpct_el0();
}

// Get timer frequency in Hz
uint64_t timer_get_frequency(platform_t *platform) {
  return platform->timer_freq_hz;
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
  write_cntp_ctl_el0(0);

  // Set timer value (relative to current counter)
  write_cntp_tval_el0(ticks);

  // Enable timer
  // Bit 0 = ENABLE (timer enabled)
  // Bit 1 = IMASK (interrupt not masked)
  // Bit 2 = ISTATUS (read-only, condition met status)
  write_cntp_ctl_el0(1); // Enable timer, interrupt unmasked
}

// Get current time in milliseconds
uint64_t timer_get_current_time_ms(platform_t *platform) {
  uint64_t counter_now = read_cntpct_el0();
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
  write_cntp_ctl_el0(0); // Disable timer (clear ENABLE and IMASK bits)
}

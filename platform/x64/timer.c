// x64 Local APIC Timer Driver
// Uses the CPU's Local APIC timer for one-shot and periodic timers

#include "timer.h"
#include "printk.h"

// Local APIC register offsets from base address
// Base address will be read from IA32_APIC_BASE MSR during init
static uint64_t g_lapic_base = 0xFEE00000UL; // Default, updated during init

#define LAPIC_ID 0x020
#define LAPIC_EOI 0x0B0
#define LAPIC_SPURIOUS 0x0F0
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CURRENT 0x390
#define LAPIC_TIMER_DIV 0x3E0

// Timer modes
#define TIMER_MODE_ONESHOT 0x00000000
#define TIMER_MODE_PERIODIC 0x00020000

// Memory-mapped register access helpers
static inline void lapic_write(uint32_t reg, uint32_t value) {
  *(volatile uint32_t *)(g_lapic_base + reg) = value;
}

static inline uint32_t lapic_read(uint32_t reg) {
  return *(volatile uint32_t *)(g_lapic_base + reg);
}

// Read MSR helper
static inline uint64_t read_msr(uint32_t msr) {
  uint32_t low, high;
  __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
  return ((uint64_t)high << 32) | low;
}

// Write MSR helper
static inline void write_msr(uint32_t msr, uint64_t value) {
  uint32_t low = value & 0xFFFFFFFF;
  uint32_t high = (value >> 32) & 0xFFFFFFFF;
  __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

// Global callback function pointer
static timer_callback_t g_timer_callback = NULL;

// Calibrated timer frequency (ticks per millisecond)
// Will be calibrated during initialization
static uint32_t g_ticks_per_ms = 62500; // Default: ~1GHz bus / 16 divisor

// Time tracking using RDTSC
static uint64_t g_tsc_start = 0;
static uint64_t g_tsc_per_ms = 0;

// Read TSC (Time Stamp Counter)
static inline uint64_t rdtsc(void) {
  uint32_t low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  return ((uint64_t)high << 32) | low;
}

// Timer interrupt handler (called from interrupt.c)
void lapic_timer_handler(void) {
  // Mask the timer to prevent further interrupts
  lapic_write(LAPIC_LVT_TIMER, 32 | 0x10000);

  // Send EOI (End of Interrupt) to Local APIC FIRST
  lapic_write(LAPIC_EOI, 0);

  // Call the user's callback if set
  if (g_timer_callback) {
    timer_callback_t cb = g_timer_callback;
    g_timer_callback = NULL; // Clear before calling
    cb();
  }
}

// IA32_APIC_BASE MSR
#define MSR_IA32_APIC_BASE 0x1B
#define APIC_BASE_ENABLE (1 << 11)

// PIT (Programmable Interval Timer) I/O ports
#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND 0x43

// PIT frequency: 1.193182 MHz (fixed, crystal oscillator)
#define PIT_FREQUENCY 1193182UL

// I/O port helpers
static inline void outb(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
  uint8_t value;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

// Calibrate the LAPIC timer frequency using PIT
// PIT runs at fixed 1.193182 MHz, making it ideal for calibration
static void calibrate_timer(void) {
  // Set divisor to 16
  lapic_write(LAPIC_TIMER_DIV, 0x3);

  // Disable timer during calibration (set vector 32, masked)
  lapic_write(LAPIC_LVT_TIMER, 32 | 0x10000);

  // Configure PIT channel 0 for one-shot mode
  // Command: Channel 0, Access mode lobyte/hibyte, Mode 0 (interrupt on
  // terminal count), Binary
  outb(PIT_COMMAND, 0x30);

  // Set PIT to count for ~10ms
  // PIT freq = 1193182 Hz, so for 10ms: count = 1193182 / 100 = 11931.82 ≈
  // 11932
  uint16_t pit_count = 11932;
  outb(PIT_CHANNEL0, pit_count & 0xFF);        // Low byte
  outb(PIT_CHANNEL0, (pit_count >> 8) & 0xFF); // High byte

  // Start LAPIC timer at maximum value
  lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFF);

  // Also read TSC at start for TSC calibration
  uint64_t tsc_before = rdtsc();

  // Wait for PIT to finish counting down to near zero
  // Add timeout to prevent infinite loop if PIT fails
  uint32_t timeout = 1000000; // Large enough for legitimate delays
  while (timeout-- > 0) {
    outb(PIT_COMMAND, 0x00); // Latch channel 0 count
    uint8_t low = inb(PIT_CHANNEL0);
    uint8_t high = inb(PIT_CHANNEL0);
    uint16_t current = (high << 8) | low;

    // Check if counter has counted down to near zero
    if (current < 10) { // Changed to 10 for better accuracy
      break;
    }
  }

  // Read TSC at end
  uint64_t tsc_after = rdtsc();

  if (timeout == 0) {
    printk("WARNING: PIT calibration timeout, using default ticks/ms\n");
  }

  // Read LAPIC timer counter value
  uint32_t lapic_elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CURRENT);

  // Stop LAPIC timer
  lapic_write(LAPIC_LVT_TIMER, 32 | 0x10000);
  lapic_write(LAPIC_TIMER_INIT, 0);

  // Calculate LAPIC ticks per ms
  // We measured ~10ms, so divide by 10
  if (lapic_elapsed > 0) {
    g_ticks_per_ms = lapic_elapsed / 10;
    printk("Timer calibrated: ");
    printk_dec(g_ticks_per_ms);
    printk(" LAPIC ticks/ms (PIT-based)\n");
  } else {
    printk("Timer calibration failed, using default ");
    printk_dec(g_ticks_per_ms);
    printk(" ticks/ms\n");
  }

  // Calculate TSC ticks per ms
  uint64_t tsc_elapsed = tsc_after - tsc_before;
  if (tsc_elapsed > 0) {
    g_tsc_per_ms = tsc_elapsed / 10;
    printk("TSC calibrated: ");
    printk_dec((uint32_t)(g_tsc_per_ms / 1000));
    printk(" MHz\n");
  } else {
    // Default to 1GHz if calibration fails
    g_tsc_per_ms = 1000000;
    printk("TSC calibration failed, assuming 1GHz\n");
  }
}

// Initialize Local APIC Timer
void timer_init(void) {
  // Read and verify LAPIC base address from MSR
  uint64_t apic_base_msr = read_msr(MSR_IA32_APIC_BASE);

  // Extract base address (bits 12-MAXPHYADDR-1, page-aligned)
  // Mask off lower 12 bits (page offset) and upper bits
  g_lapic_base = apic_base_msr & 0xFFFFFFFFFF000ULL;

  // Validate LAPIC base address
  if (g_lapic_base == 0 || g_lapic_base < 0x1000) {
    printk("ERROR: Invalid LAPIC base address: ");
    printk_hex64(g_lapic_base);
    printk("\n");
    printk("LAPIC may not be supported on this system\n");
    return;
  }

  printk("LAPIC base address: ");
  printk_hex64(g_lapic_base);
  printk("\n");

  // Check and enable LAPIC via MSR if not already enabled
  if (!(apic_base_msr & APIC_BASE_ENABLE)) {
    write_msr(MSR_IA32_APIC_BASE, apic_base_msr | APIC_BASE_ENABLE);
    printk("LAPIC enabled via MSR\n");
  }

  // Enable Local APIC by setting spurious interrupt vector
  // Bit 8 = APIC Software Enable, Vector = 0xFF
  lapic_write(LAPIC_SPURIOUS, 0x1FF);

  // Set timer divisor to 16 (bits 0-1 = 0b11, bits 2-3 = 0b00)
  lapic_write(LAPIC_TIMER_DIV, 0x3);

  printk("Local APIC timer initialized (LAPIC ID 0x");
  printk_hex32(lapic_read(LAPIC_ID));
  printk(")\n");

  // Calibrate timer frequency
  calibrate_timer();

  // Capture start time
  g_tsc_start = rdtsc();
}

// Get current time in milliseconds
uint64_t timer_get_current_time_ms(void) {
  uint64_t tsc_now = rdtsc();
  uint64_t tsc_elapsed = tsc_now - g_tsc_start;

  if (g_tsc_per_ms == 0) {
    return 0;
  }

  return tsc_elapsed / g_tsc_per_ms;
}

// Send EOI (End Of Interrupt) to Local APIC
void lapic_send_eoi(void) { lapic_write(LAPIC_EOI, 0); }

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(uint32_t milliseconds, timer_callback_t callback) {
  if (callback == NULL) {
    printk("timer_set_oneshot_ms: NULL callback\n");
    return;
  }

  g_timer_callback = callback;

  // Calculate tick count
  uint32_t ticks = milliseconds * g_ticks_per_ms;

  // Configure LVT Timer Register:
  // - Vector 32 (our timer interrupt)
  // - One-shot mode (bit 17 = 0)
  // - Not masked (bit 16 = 0)
  lapic_write(LAPIC_LVT_TIMER, 32 | TIMER_MODE_ONESHOT);

  // Set initial count (starts the timer)
  lapic_write(LAPIC_TIMER_INIT, ticks);
}

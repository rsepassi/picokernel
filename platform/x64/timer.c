// x64 Local APIC Timer Driver
// Uses the CPU's Local APIC timer for one-shot and periodic timers

#include "timer.h"
#include "printk.h"
#include <stdint.h>

#define NULL ((void*)0)

// Local APIC register offsets from base address
// Base address will be read from IA32_APIC_BASE MSR during init
static uint64_t g_lapic_base = 0xFEE00000UL;  // Default, updated during init

#define LAPIC_ID            0x020
#define LAPIC_EOI           0x0B0
#define LAPIC_SPURIOUS      0x0F0
#define LAPIC_LVT_TIMER     0x320
#define LAPIC_TIMER_INIT    0x380
#define LAPIC_TIMER_CURRENT 0x390
#define LAPIC_TIMER_DIV     0x3E0

// Timer modes
#define TIMER_MODE_ONESHOT  0x00000000
#define TIMER_MODE_PERIODIC 0x00020000

// Memory-mapped register access helpers
static inline void lapic_write(uint32_t reg, uint32_t value)
{
    *(volatile uint32_t*)(g_lapic_base + reg) = value;
}

static inline uint32_t lapic_read(uint32_t reg)
{
    return *(volatile uint32_t*)(g_lapic_base + reg);
}

// Read MSR helper
static inline uint64_t read_msr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

// Write MSR helper
static inline void write_msr(uint32_t msr, uint64_t value)
{
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = (value >> 32) & 0xFFFFFFFF;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

// Global callback function pointer
static timer_callback_t g_timer_callback = NULL;

// Calibrated timer frequency (ticks per millisecond)
// Will be calibrated during initialization
static uint32_t g_ticks_per_ms = 62500;  // Default: ~1GHz bus / 16 divisor

// Timer interrupt handler (called from interrupt.c)
void lapic_timer_handler(void)
{
    // Mask the timer to prevent further interrupts
    lapic_write(LAPIC_LVT_TIMER, 32 | 0x10000);

    // Send EOI (End of Interrupt) to Local APIC FIRST
    lapic_write(LAPIC_EOI, 0);

    // Call the user's callback if set
    if (g_timer_callback) {
        timer_callback_t cb = g_timer_callback;
        g_timer_callback = NULL;  // Clear before calling
        cb();
    }
}

// IA32_APIC_BASE MSR
#define MSR_IA32_APIC_BASE 0x1B
#define APIC_BASE_ENABLE (1 << 11)

// Simple delay loop for calibration (approximately 10ms)
static void delay_loop(uint32_t iterations)
{
    for (volatile uint32_t i = 0; i < iterations; i++) {
        __asm__ volatile("pause");
    }
}

// Calibrate the LAPIC timer frequency
static void calibrate_timer(void)
{
    // Set divisor to 16
    lapic_write(LAPIC_TIMER_DIV, 0x3);

    // Disable timer during calibration (set vector 32, masked)
    lapic_write(LAPIC_LVT_TIMER, 32 | 0x10000);

    // Set initial count to maximum value
    lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFF);

    // Wait for a short period (rough ~10ms delay)
    delay_loop(1000000);

    // Read current counter value
    uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CURRENT);

    // Stop timer (keep vector 32, masked)
    lapic_write(LAPIC_LVT_TIMER, 32 | 0x10000);
    lapic_write(LAPIC_TIMER_INIT, 0);

    // Estimate ticks per ms (elapsed in ~10ms, so divide by 10)
    // Add some margin for safety (use 80% of measured value)
    if (elapsed > 0) {
        g_ticks_per_ms = (elapsed / 10) * 4 / 5;
        printk("Timer calibrated: ~");
        printk_dec(g_ticks_per_ms);
        printk(" ticks/ms\n");
    } else {
        printk("Timer calibration failed, using default ");
        printk_dec(g_ticks_per_ms);
        printk(" ticks/ms\n");
    }
}

// Initialize Local APIC Timer
void timer_init(void)
{
    // Read and verify LAPIC base address from MSR
    uint64_t apic_base_msr = read_msr(MSR_IA32_APIC_BASE);

    // Extract base address (bits 12-35, page-aligned)
    g_lapic_base = apic_base_msr & 0xFFFFF000UL;

    printk("LAPIC base address: 0x");
    printk_hex32((uint32_t)g_lapic_base);
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
}

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(uint32_t milliseconds, timer_callback_t callback)
{
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

    printk("Timer set for ");
    printk_dec(milliseconds);
    printk("ms (");
    printk_dec(ticks);
    printk(" ticks)\n");
}

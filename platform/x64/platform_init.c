// x64 Platform Initialization
// Sets up interrupts and timer with a demo callback

#include "interrupt.h"
#include "timer.h"
#include "printk.h"

// Global flag set by timer
volatile int timer_fired = 0;

// Timer callback function - just set a flag
static void timer_callback(void)
{
    timer_fired = 1;
}

// Platform-specific initialization
void platform_timer_init(void)
{
    printk("Initializing x64 interrupts and timer...\n");

    // Initialize interrupt handling (IDT)
    interrupt_init();

    // Initialize Local APIC timer
    timer_init();

    // Set a short 1000ms one-shot timer for testing
    timer_set_oneshot_ms(1000, timer_callback);

    // Enable interrupts globally
    interrupt_enable();

    printk("Interrupts enabled. Waiting for timer...\n\n");
}

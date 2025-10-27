// vmos kernel entry point

#include "printk.h"
#include "platform.h"

extern volatile int timer_fired;

void main(void* fdt)
{
    printk("vmos kernel starting...\n\n");

    // Parse and display device tree
    fdt_dump(fdt);

    printk("Kernel initialization complete.\n\n");

    // Initialize platform-specific timer (if available)
    platform_timer_init();

    // Halt CPU until timer interrupt fires
    printk("\n\nWaiting for timer interrupt...\n\n");
    while (!timer_fired) {
        cpu_halt();  // Halt CPU until interrupt wakes it up
    }
    printk("Timer interrupt fired successfully!\n");
    printk("Interrupt handler returned to main loop.\n\n");

    // Just halt forever after demonstrating the timer works
    printk("Test complete. Halting forever...\n");
    while (1) {
        cpu_halt();  // Idle loop - halt until next interrupt
    }
}

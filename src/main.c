// vmos kernel entry point

#include "printk.h"

void fdt_dump(void* fdt);
void platform_timer_init(void);  // Platform-specific timer initialization

volatile int back_in_loop = 0;
extern volatile int timer_fired;

void main(void* fdt)
{
    printk("vmos kernel starting...\n\n");

    // Parse and display device tree
    fdt_dump(fdt);

    printk("Kernel initialization complete.\n\n");

    // Initialize platform-specific timer (if available)
    platform_timer_init();

    // Infinite loop - check for timer
    printk("\n\nWaiting for timer interrupt...\n\n");
    while (1) {
        if (timer_fired) {
            printk("Timer interrupt fired successfully!\n");
            printk("Interrupt handler returned to main loop.\n\n");
            break;
        }
    }

    // Just spin forever after demonstrating the timer works
    printk("Test complete. Spinning forever...\n");
    while (1) {
        // Infinite loop - kernel stays alive
    }
}

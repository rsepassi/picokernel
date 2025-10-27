// vmos kernel entry point

#include "platform.h"
#include "printk.h"

// Kernel state structure
typedef struct {
    platform_t platform;
} kernel_t;

void main(void* fdt)
{
    printk("vmos kernel starting...\n\n");

    kernel_t k;

    // Initialize platform (interrupts, timers, device enumeration)
    platform_init(&k.platform, fdt);
    printk("Kernel initialization complete.\n\n");

    // Wait for interrupt with 1000ms timeout
    printk("\n\nWaiting for interrupt (1000ms timeout)...\n\n");
    uint32_t reason = platform_wfi(&k.platform, 1000);
    printk("Interrupt received: ");
    printk(platform_int_reason_str(reason));
    printk("\n");
    printk("Interrupt handler returned to main loop.\n\n");

    printk("kloop...\n");
    while (1) {
      uint32_t reason = platform_wfi(&k.platform, UINT64_MAX);
      printk("Interrupt received: ");
      printk(platform_int_reason_str(reason));
      printk("\n");
    }
}

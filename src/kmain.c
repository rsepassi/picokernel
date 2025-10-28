// vmos kernel entry point

#include "kernel.h"
#include "user.h"
#include "printk.h"

#define MAX_TIMEOUT 2000

void kmain(void* fdt)
{
    printk("=== VMOS KMAIN === \n\n");

    // Initialize
    kernel_t k;
    kinit(&k, fdt);
    printk("[KMAIN] kinit ok\n");

    // User kickoff
    kuser_t user;
    user.kernel = &k;
    kusermain(&user);
    printk("[KMAIN] kusermain ok\n");

    // Event loop
    interrupt_enable();
    printk("[KMAIN] kloop...\n");
    uint64_t current_time = 0;
    while (1) {
      printk("[KLOOP] tick\n");
        ktick(&k, current_time);
        uint64_t timeout = knext_delay(&k);
        if (timeout > MAX_TIMEOUT) timeout = MAX_TIMEOUT;
        printk("[KLOOP] wfi\n");
        current_time = platform_wfi(&k.platform, timeout);
    }
}

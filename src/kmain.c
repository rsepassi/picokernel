// vmos kernel entry point

#include "kernel.h"
#include "printk.h"
#include "user.h"

#define MAX_TIMEOUT 2000

void kmain(void *fdt) {
  printk("=== VMOS KMAIN === \n\n");

  // Initialize
  kernel_t k;
  kmain_init(&k, fdt);
  printk("[KMAIN] kmain_init ok\n");

  // User kickoff
  kuser_t user;
  user.kernel = &k;
  kmain_usermain(&user);
  printk("[KMAIN] kmain_usermain ok\n");

  // Event loop
  platform_interrupt_enable();
  printk("[KMAIN] kloop...\n");
  uint64_t current_time = 0;
  while (1) {
    printk("[KLOOP] tick\n");
    kmain_tick(&k, current_time);
    uint64_t timeout = kmain_next_delay(&k);
    if (timeout > MAX_TIMEOUT)
      timeout = MAX_TIMEOUT;
    printk("[KLOOP] wfi\n");
    current_time = platform_wfi(&k.platform, timeout);
  }
}

// vmos kernel entry point

#include "kbase.h"
#include "kernel.h"
#include "printk.h"
#include "user.h"

static kernel_t g_kernel;

void kmain(void *fdt) {
  printk("\n\n=== VMOS KMAIN ===\n\n");

  // Initialize
  kernel_t *k = &g_kernel;
  kmain_init(k, fdt);
  KLOG("kmain_init ok");

  // User kickoff
  kuser_t user;
  user.kernel = k;
  kmain_usermain(&user);
  KLOG("kmain_usermain ok");

  // Event loop
  KLOG("kloop...");
  while (1) {
    kmain_step(k, 2000);
  }
}

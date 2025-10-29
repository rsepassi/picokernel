// vmos kernel entry point

#include "kernel.h"
#include "printk.h"
#include "user.h"

#define MAX_TIMEOUT 2000

void kmain(void *fdt) {
  printk("\n\n=== VMOS KMAIN ===\n\n");

  // Initialize
  kernel_t k;
  kmain_init(&k, fdt);
  printk("[KMAIN] kmain_init ok\n");

  // Enable interrupts (required for virtio-rng)
  platform_interrupt_enable();
  printk("[KMAIN] interrupts enabled\n");

  // Initialize CSPRNG with strong entropy
  kcsprng_init_state_t csprng_state;
  kmain_init_csprng(&k, &csprng_state);
  printk("[KMAIN] CSPRNG ready\n");

  // User kickoff
  kuser_t user;
  user.kernel = &k;
  kmain_usermain(&user);
  printk("[KMAIN] kmain_usermain ok\n");

  // Event loop
  printk("[KMAIN] kloop...\n");
  while (1) {
    kmain_step(&k, MAX_TIMEOUT);
  }
}

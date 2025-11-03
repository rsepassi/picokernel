// vmos kernel entry point

#include "kbase.h"
#include "kernel.h"
#include "mem_debug.h"
#include "printk.h"
#include "user.h"

#define MAX_TICK_TIMEOUT 2000

static kernel_t g_kernel;
static user_t g_user;

// Internal getter for logging/debug purposes only
// DO NOT use this for general kernel access - pass kernel_t* explicitly
kernel_t *kget_kernel__logonly__(void) { return &g_kernel; }

uint64_t kget_time_ms__logonly__(void) { return g_kernel.current_time_ms; }

void kmain(void *fdt) {
  printk("\n\n=== VMOS KMAIN ===\n\n");

  // Early boot validation (validates boot.S results)
  KDEBUG_VALIDATE(platform_mem_validate_critical());

  // Initialize
  kernel_t *k = &g_kernel;
  kmain_init(k, fdt);
  KLOG("kmain_init ok");

  // Post-init validation (validates device initialization)
  KDEBUG_VALIDATE(platform_mem_validate_post_init(&k->platform, fdt));

  // User kickoff
  KLOG("user_main...");
  user_t *user = &g_user;
  user->kernel = k;
  user_main(user);
  KLOG("user_main ok");

  // Event loop
  KLOG("kloop...");
  while (1) {
  KLOG("[KLOOP] tick");
  kmain_tick(k, k->current_time_ms);
  uint64_t timeout = kmain_next_delay(k);
  if (timeout > MAX_TICK_TIMEOUT)
    timeout = MAX_TICK_TIMEOUT;
  KLOG("[KLOOP] wfi");
  k->current_time_ms = platform_wfi(&k->platform, timeout);
  }
}

#include "kbase.h"
#include "platform.h"
#include "printk.h"

void kpanic(const char *msg) {
  printk("\n=== KERNEL PANIC ===\n");
  printk(msg);
  printk("\n\n");

  // Platform provides register dump
  platform_dump_registers();

  // Stack dump (64 bytes from current SP)
  platform_dump_stack(64);

#ifdef KDEBUG
  // Last completed work items (ring buffer)
  kdebug_dump_work_history();
#endif

  // Halt: call platform abort then infinite loop as safety
  platform_abort();

  // Should never reach here, but add infinite loop as safety
  while (1) {
    __asm__ volatile("" ::: "memory");
  }
}

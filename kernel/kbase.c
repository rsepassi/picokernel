#include "kbase.h"
#include "platform.h"

// Forward declare printk functions (defined in kernel/)
extern void printk(const char *s);

void *memcpy(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < n; i++) {
    d[i] = s[i];
  }
  return dest;
}

void *memset(void *s, int c, size_t n) {
  uint8_t *p = (uint8_t *)s;
  for (size_t i = 0; i < n; i++) {
    p[i] = (uint8_t)c;
  }
  return s;
}

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

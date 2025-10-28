// x64 Platform Hooks Implementation
// Architecture-specific operations for VirtIO abstraction layer

#include "interrupt.h"
#include "platform.h"
#include <stddef.h>

// x86-64 has hardware cache coherency for DMA
// No need to manually flush/invalidate caches for device access
void platform_cache_clean(void *addr, size_t size) {
  // No-op: x86-64 maintains cache coherency automatically
  (void)addr;
  (void)size;
}

void platform_cache_invalidate(void *addr, size_t size) {
  // No-op: x86-64 maintains cache coherency automatically
  (void)addr;
  (void)size;
}

// Full memory barrier using mfence instruction
void platform_memory_barrier(void) { __asm__ volatile("mfence" ::: "memory"); }

// IRQ registration
int platform_irq_register(uint32_t irq_num, void (*handler)(void *),
                          void *context) {
  irq_register((uint8_t)irq_num, handler, context);
  return 0;
}

// IRQ enable
void platform_irq_enable(uint32_t irq_num) { irq_enable((uint8_t)irq_num); }

// ARM32 Platform Hooks Implementation
// Architecture-specific operations for VirtIO abstraction layer

#include "interrupt.h"
#include "platform.h"
#include <stddef.h>
#include <stdint.h>

// ARM32 cache line size
#define CACHE_LINE_SIZE 64

// ARM32 Cache Maintenance Operations
// These are critical for DMA coherency - the device reads/writes RAM directly,
// so we must ensure CPU cache is synchronized with RAM

// Clean (flush) cache for a memory range - pushes CPU writes to RAM
// Call this AFTER CPU writes data that the device will read
void platform_cache_clean(void *addr, size_t size) {
  (void)addr;
  (void)size;
  // QEMU ARM virt machine has hardware cache coherency (dma-coherent in DT)
  // Cache operations are no-op, just need a barrier
  __asm__ volatile("dsb sy" ::: "memory");
}

// Invalidate cache for a memory range - discards stale CPU cache
// Call this BEFORE CPU reads data that the device has written
void platform_cache_invalidate(void *addr, size_t size) {
  (void)addr;
  (void)size;
  // QEMU ARM virt machine has hardware cache coherency (dma-coherent in DT)
  // Cache operations are no-op, just need a barrier
  __asm__ volatile("dsb sy" ::: "memory");
}

// Full memory barrier using ARM32 DSB instruction
void platform_memory_barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }

// IRQ registration
int platform_irq_register(uint32_t irq_num, void (*handler)(void *),
                          void *context) {
  irq_register(irq_num, handler, context);
  return 0;
}

// IRQ enable
void platform_irq_enable(uint32_t irq_num) { irq_enable(irq_num); }

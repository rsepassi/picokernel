// ARM64 Platform Hooks Implementation
// Architecture-specific operations for VirtIO abstraction layer

#include "platform_hooks.h"
#include "interrupt.h"
#include <stddef.h>
#include <stdint.h>

// ARM64 cache line size
#define CACHE_LINE_SIZE 64

// ARM64 Cache Maintenance Operations
// These are critical for DMA coherency - the device reads/writes RAM directly,
// so we must ensure CPU cache is synchronized with RAM

// Clean (flush) cache for a memory range - pushes CPU writes to RAM
// Call this AFTER CPU writes data that the device will read
void platform_cache_clean(void *addr, size_t size) {
  uintptr_t start = (uintptr_t)addr;
  uintptr_t end = start + size;

  // Align down to cache line boundary
  start &= ~(CACHE_LINE_SIZE - 1);

  // Clean each cache line in the range
  for (uintptr_t va = start; va < end; va += CACHE_LINE_SIZE) {
    __asm__ volatile("dc cvac, %0" ::"r"(va) : "memory");
  }

  // Ensure all cache operations complete
  __asm__ volatile("dsb sy" ::: "memory");
}

// Invalidate cache for a memory range - discards stale CPU cache
// Call this BEFORE CPU reads data that the device has written
void platform_cache_invalidate(void *addr, size_t size) {
  uintptr_t start = (uintptr_t)addr;
  uintptr_t end = start + size;

  // Align down to cache line boundary
  start &= ~(CACHE_LINE_SIZE - 1);

  // Invalidate each cache line in the range
  for (uintptr_t va = start; va < end; va += CACHE_LINE_SIZE) {
    __asm__ volatile("dc ivac, %0" ::"r"(va) : "memory");
  }

  // Ensure all cache operations complete
  __asm__ volatile("dsb sy" ::: "memory");
}

// Full memory barrier using ARM64 DSB instruction
void platform_memory_barrier(void) {
  __asm__ volatile("dsb sy" ::: "memory");
}

// IRQ registration
int platform_irq_register(uint32_t irq_num, void (*handler)(void *),
                          void *context) {
  irq_register(irq_num, handler, context);
  return 0;
}

// IRQ enable
void platform_irq_enable(uint32_t irq_num) { irq_enable(irq_num); }

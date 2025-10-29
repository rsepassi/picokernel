// RISC-V 64-bit Platform Hooks Implementation
// Architecture-specific operations for VirtIO abstraction layer

#include "interrupt.h"
#include "platform.h"
#include <stddef.h>
#include <stdint.h>

// RISC-V cache line size (typical)
#define CACHE_LINE_SIZE 64

// RISC-V Cache Maintenance Operations
// These are critical for DMA coherency - the device reads/writes RAM directly,
// so we must ensure CPU cache is synchronized with RAM

// Clean (flush) cache for a memory range - pushes CPU writes to RAM
// Call this AFTER CPU writes data that the device will read
void platform_cache_clean(void *addr, size_t size) {
  // RISC-V fence instructions for memory ordering
  // fence iorw, iorw - full fence for all memory operations
  __asm__ volatile("fence iorw, iorw" ::: "memory");

  // Note: RISC-V standard doesn't define cache management instructions
  // in the base ISA. Some implementations may need SBI calls or
  // custom cache management extensions. For QEMU virt, the fence
  // instruction is typically sufficient as the hardware maintains
  // coherency or has write-through caches.

  (void)addr; // Unused - fence affects all memory
  (void)size;
}

// Invalidate cache for a memory range - discards stale CPU cache
// Call this BEFORE CPU reads data that the device has written
void platform_cache_invalidate(void *addr, size_t size) {
  // RISC-V fence instructions for memory ordering
  // fence.i is for instruction cache, fence iorw,iorw for data
  __asm__ volatile("fence iorw, iorw" ::: "memory");

  // Note: Similar to cache_clean, RISC-V relies on fence instructions.
  // Some implementations may need additional operations.

  (void)addr; // Unused - fence affects all memory
  (void)size;
}

// Full memory barrier using RISC-V fence instruction
void platform_memory_barrier(void) {
  __asm__ volatile("fence iorw, iorw" ::: "memory");
}

// IRQ registration
int platform_irq_register(uint32_t irq_num, void (*handler)(void *),
                          void *context) {
  irq_register(irq_num, handler, context);
  return 0;
}

// IRQ enable
void platform_irq_enable(uint32_t irq_num) { irq_enable(irq_num); }

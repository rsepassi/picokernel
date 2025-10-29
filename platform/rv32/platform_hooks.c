// RV32 Platform Hooks Implementation
// Architecture-specific operations for VirtIO abstraction layer

#include "interrupt.h"
#include "platform.h"
#include "sbi.h"
#include <stddef.h>
#include <stdint.h>

// RISC-V Cache Management
// RISC-V uses fence instructions for memory ordering and cache coherency
// For DMA coherency, we need to ensure ordering between CPU and device operations

// Clean (flush) cache for a memory range - pushes CPU writes to RAM
// Call this AFTER CPU writes data that the device will read
void platform_cache_clean(void *addr, size_t size) {
  (void)addr; // Address not used for RISC-V fence
  (void)size; // Size not used for RISC-V fence

  // FENCE instruction ensures all prior memory writes are visible
  // to all subsequent memory operations (including DMA)
  // FENCE rw,rw: Read/Write -> Read/Write ordering
  __asm__ volatile("fence rw,rw" ::: "memory");
}

// Invalidate cache for a memory range - discards stale CPU cache
// Call this BEFORE CPU reads data that the device has written
void platform_cache_invalidate(void *addr, size_t size) {
  (void)addr; // Address not used for RISC-V fence
  (void)size; // Size not used for RISC-V fence

  // FENCE instruction ensures all prior memory operations (including DMA)
  // complete before any subsequent memory reads
  __asm__ volatile("fence rw,rw" ::: "memory");

  // Note: Some RISC-V implementations may require explicit cache
  // invalidation via SBI calls or custom instructions. For QEMU virt,
  // the simple fence is sufficient as caches are coherent.
  // If running on real hardware with non-coherent caches, additional
  // SBI calls may be needed here (e.g., SBI remote fence extensions).
}

// Full memory barrier using RISC-V FENCE instruction
void platform_memory_barrier(void) {
  __asm__ volatile("fence rw,rw" ::: "memory");
}

// IRQ registration
int platform_irq_register(uint32_t irq_num, void (*handler)(void *),
                          void *context) {
  irq_register(irq_num, handler, context);
  return 0;
}

// IRQ enable
void platform_irq_enable(uint32_t irq_num) { irq_enable(irq_num); }

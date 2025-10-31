// x32 MMIO Register Access with Memory Barriers
// Implements platform MMIO functions with proper synchronization

#include "platform.h"
#include <stdint.h>

// Memory barrier for MMIO operations
// Uses MFENCE (Memory Fence) to ensure:
// - MMIO operations complete before proceeding
// - No speculative reads/writes to device registers
// - Proper ordering on x86's memory model
static inline void mmio_barrier(void) {
  __asm__ volatile("mfence" ::: "memory");
}

// MMIO read functions with barriers
// Barrier AFTER read ensures read completes before next operation

uint8_t platform_mmio_read8(volatile uint8_t *addr) {
  uint8_t val = *addr;
  mmio_barrier();
  return val;
}

uint16_t platform_mmio_read16(volatile uint16_t *addr) {
  uint16_t val = *addr;
  mmio_barrier();
  return val;
}

uint32_t platform_mmio_read32(volatile uint32_t *addr) {
  uint32_t val = *addr;
  mmio_barrier();
  return val;
}

uint64_t platform_mmio_read64(volatile uint64_t *addr) {
  uint64_t val = *addr;
  mmio_barrier();
  return val;
}

// MMIO write functions with barriers
// Barrier AFTER write ensures write completes before next operation

void platform_mmio_write8(volatile uint8_t *addr, uint8_t val) {
  *addr = val;
  mmio_barrier();
}

void platform_mmio_write16(volatile uint16_t *addr, uint16_t val) {
  *addr = val;
  mmio_barrier();
}

void platform_mmio_write32(volatile uint32_t *addr, uint32_t val) {
  *addr = val;
  mmio_barrier();
}

void platform_mmio_write64(volatile uint64_t *addr, uint64_t val) {
  *addr = val;
  mmio_barrier();
}

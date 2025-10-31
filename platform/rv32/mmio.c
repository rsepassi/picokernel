// RISC-V 32-bit MMIO Register Access with Memory Barriers
// Implements platform MMIO functions with proper synchronization

#include "platform.h"
#include <stdint.h>

// Memory barrier for MMIO operations
// Uses FENCE instruction to ensure:
// - MMIO operations complete before proceeding
// - No speculative reads/writes to device registers
// - Proper ordering on RISC-V's weakly-ordered memory model
static inline void mmio_barrier(void) {
  __asm__ volatile("fence iorw, iorw" ::: "memory");
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
  // On RV32, 64-bit reads must be done as two 32-bit reads
  // Read low word first, then high word
  volatile uint32_t *addr32 = (volatile uint32_t *)addr;
  uint32_t low = addr32[0];
  uint32_t high = addr32[1];
  mmio_barrier();
  return ((uint64_t)high << 32) | low;
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
  // On RV32, 64-bit writes must be done as two 32-bit writes
  // Write low word first, then high word
  volatile uint32_t *addr32 = (volatile uint32_t *)addr;
  addr32[0] = (uint32_t)val;
  addr32[1] = (uint32_t)(val >> 32);
  mmio_barrier();
}

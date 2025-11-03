// Shared MMIO Register Access with Memory Barriers
// Implements platform MMIO functions with proper synchronization
// Platform must provide platform_mmio_barrier() inline function

#include "platform.h"
#include "platform_impl.h"

// MMIO read functions with barriers
// Barrier AFTER read ensures read completes before next operation

uint8_t platform_mmio_read8(volatile uint8_t *addr) {
  uint8_t val = *addr;
  platform_mmio_barrier();
  return val;
}

uint16_t platform_mmio_read16(volatile uint16_t *addr) {
  uint16_t val = *addr;
  platform_mmio_barrier();
  return val;
}

uint32_t platform_mmio_read32(volatile uint32_t *addr) {
  uint32_t val = *addr;
  platform_mmio_barrier();
  return val;
}

// MMIO write functions with barriers
// Barrier AFTER write ensures write completes before next operation

void platform_mmio_write8(volatile uint8_t *addr, uint8_t val) {
  *addr = val;
  platform_mmio_barrier();
}

void platform_mmio_write16(volatile uint16_t *addr, uint16_t val) {
  *addr = val;
  platform_mmio_barrier();
}

void platform_mmio_write32(volatile uint32_t *addr, uint32_t val) {
  *addr = val;
  platform_mmio_barrier();
}

// Note: platform_mmio_read/write64() is implemented as inline in
// platform_impl.h to allow platforms to handle 64-bit operations
// appropriately (e.g., 32-bit platforms may split into two 32-bit writes)

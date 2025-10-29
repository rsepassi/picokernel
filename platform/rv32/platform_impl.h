// rv32 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include <stddef.h>
#include <stdint.h>

// Forward declarations
struct virtio_rng_dev;
typedef struct virtio_rng_dev virtio_rng_dev_t;

struct kernel;
typedef struct kernel kernel_t;

// rv32 platform-specific state
typedef struct {
  uint64_t timer_freq;          // Timer frequency in Hz (from devicetree)
  uint64_t ticks_per_ms;        // Precomputed ticks per millisecond
  virtio_rng_dev_t *virtio_rng; // VirtIO-RNG device (NULL if not present)
  kernel_t *kernel;             // Back-pointer to kernel
} platform_t;

// RV32 RNG request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;

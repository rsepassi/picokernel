// x64 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include <stdint.h>

// Forward declarations
struct virtio_rng_t;
typedef struct virtio_rng_t virtio_rng_t;

struct kernel;
typedef struct kernel kernel_t;

// x64 platform-specific state
typedef struct {
  uint32_t last_interrupt;  // Last interrupt reason code
  virtio_rng_t *virtio_rng; // VirtIO-RNG device (NULL if not present)
  kernel_t *kernel;         // Back-pointer to kernel
} platform_t;

// x64 RNG request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;

// ARM64 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include <stddef.h>
#include <stdint.h>

// Forward declarations
struct virtio_rng_dev;
typedef struct virtio_rng_dev virtio_rng_dev_t;

struct kernel;
typedef struct kernel kernel_t;

// ARM64 platform-specific state
typedef struct {
  uint64_t timer_freq_hz;       // Timer frequency from CNTFRQ_EL0
  virtio_rng_dev_t *virtio_rng; // VirtIO-RNG device (NULL if not present)
  kernel_t *kernel;             // Back-pointer to kernel
} platform_t;

// ARM64 RNG request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;

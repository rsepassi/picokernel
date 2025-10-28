// ARM64 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include "virtio/virtio.h"
#include "virtio/virtio_mmio.h"
#include "virtio/virtio_rng.h"

// VirtIO virtqueue memory size (64KB, page-aligned for DMA)
#define VIRTQUEUE_MEMORY_SIZE (64 * 1024)

// ARM64 RNG request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;

// ARM64 platform-specific state
typedef struct {
  uint64_t timer_freq_hz; // Timer frequency from CNTFRQ_EL0
  void *kernel;           // Back-pointer to kernel

  // VirtIO MMIO transport
  virtio_mmio_transport_t virtio_mmio;

  // VirtIO-RNG device state
  virtio_rng_dev_t virtio_rng;
  uint8_t virtqueue_memory[VIRTQUEUE_MEMORY_SIZE]
      __attribute__((aligned(4096)));
  int virtio_rng_present; // 1 if device initialized, 0 otherwise
} platform_t;

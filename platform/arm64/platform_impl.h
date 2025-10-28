// ARM64 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include "virtio/virtio.h"

// VirtIO virtqueue memory size (64KB, page-aligned for DMA)
#define VIRTQUEUE_MEMORY_SIZE (64 * 1024)

// VirtIO-RNG device state
#define VIRTIO_RNG_MAX_REQUESTS 256

// ARM64 RNG request platform-specific fields (VirtIO)
typedef struct {
    uint16_t desc_idx;  // VirtIO descriptor index
} krng_req_platform_t;

typedef struct {
    // MMIO base address
    volatile uint8_t* mmio_base;
    uint64_t mmio_size;

    // IRQ routing
    uint32_t irq_num;  // GIC IRQ number

    // Virtqueue
    virtqueue_t vq;
    uint16_t queue_size;
    void* vq_memory;  // Allocated memory for virtqueue

    // Request tracking (constant-time lookup)
    // Using void* to avoid including kapi.h here (cast to krng_req_t* in virtio_mmio.c)
    void* active_requests[VIRTIO_RNG_MAX_REQUESTS];

    // Interrupt pending flag (set by ISR, cleared by ktick)
    volatile uint8_t irq_pending;
} virtio_rng_t;

// ARM64 platform-specific state
typedef struct {
    uint64_t timer_freq_hz;    // Timer frequency from CNTFRQ_EL0
    void* kernel;              // Back-pointer to kernel

    // VirtIO-RNG device state
    virtio_rng_t virtio_rng;
    uint8_t virtqueue_memory[VIRTQUEUE_MEMORY_SIZE] __attribute__((aligned(4096)));
    int virtio_rng_present;    // 1 if device initialized, 0 otherwise
} platform_t;

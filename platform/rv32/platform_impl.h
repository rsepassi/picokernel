// rv32 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include <stddef.h>
#include <stdint.h>

// Include VirtIO headers for complete type definitions
// This is needed to embed VirtIO structures in platform_t
#include "virtio/virtio_mmio.h"
#include "virtio/virtio_pci.h"
#include "virtio/virtio_rng.h"

// Forward declarations
struct kernel;
typedef struct kernel kernel_t;

// Timer callback function type
typedef void (*timer_callback_t)(void);

// IRQ handler table entry
typedef struct {
  void *context;
  void (*handler)(void *context);
} irq_entry_t;

// Maximum number of IRQs supported by PLIC
#define MAX_IRQS 128

// rv32 platform-specific state
typedef struct platform_t {
  // Timer state
  uint64_t timer_freq;             // Timer frequency in Hz (from devicetree)
  uint64_t ticks_per_ms;           // Precomputed ticks per millisecond
  uint64_t timer_start;            // Start time for timer_get_current_time_ms()
  timer_callback_t timer_callback; // Timer callback function pointer
  volatile int timer_fired; // Flag indicating timer has fired (rv32-specific)

  // VirtIO device state
  virtio_pci_transport_t virtio_pci_transport;
  virtio_mmio_transport_t virtio_mmio_transport;
  virtio_rng_dev_t virtio_rng;
  virtqueue_memory_t virtqueue_memory; // VirtIO queue memory
  virtio_rng_dev_t
      *virtio_rng_ptr; // Pointer to active RNG device (NULL if not present)

  // Interrupt state
  irq_entry_t irq_table[MAX_IRQS];

  // Back-pointer to kernel
  kernel_t *kernel;
} platform_t;

// RV32 RNG request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;

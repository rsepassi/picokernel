// Generic VirtIO RNG Device Driver
// Transport-agnostic RNG driver (works with MMIO or PCI)

#pragma once

#include "kconfig.h"
#include "platform_hooks.h"
#include "virtio.h"
#include "virtio_mmio.h"
#include "virtio_pci.h"
#include <stddef.h>
#include <stdint.h>

// Forward declarations
struct kernel;
typedef struct kernel kernel_t;
struct kwork;
typedef struct kwork kwork_t;

// Transport type
#define VIRTIO_TRANSPORT_MMIO 1
#define VIRTIO_TRANSPORT_PCI 2

// VirtIO RNG device state
#define VIRTIO_RNG_MAX_REQUESTS 256

typedef struct virtio_rng_dev {
  // Transport (either MMIO or PCI)
  void *transport;    // Points to transport-specific structure
  int transport_type; // VIRTIO_TRANSPORT_MMIO or VIRTIO_TRANSPORT_PCI

  // Virtqueue
  virtqueue_t vq;
  void *vq_memory;
  uint16_t queue_size;

  // Request tracking (constant-time lookup)
  // Use void* to avoid circular dependency
  void *active_requests[VIRTIO_RNG_MAX_REQUESTS];

  // Interrupt pending flag (set by ISR, cleared by processing)
  volatile uint8_t irq_pending;

  // Kernel reference
  kernel_t *kernel;
} virtio_rng_dev_t;

// Initialize RNG with MMIO transport
int virtio_rng_init_mmio(virtio_rng_dev_t *rng, virtio_mmio_transport_t *mmio,
                         void *queue_memory, kernel_t *kernel);

// Initialize RNG with PCI transport
int virtio_rng_init_pci(virtio_rng_dev_t *rng, virtio_pci_transport_t *pci,
                        void *queue_memory, kernel_t *kernel);

// Process interrupt (transport-agnostic)
void virtio_rng_process_irq(virtio_rng_dev_t *rng, kernel_t *k);

// Submit work (transport-agnostic)
void virtio_rng_submit_work(virtio_rng_dev_t *rng, kwork_t *submissions,
                            kernel_t *k);

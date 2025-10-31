// Generic VirtIO MMIO Transport Implementation
// Platform-agnostic MMIO transport using platform hooks

#include "virtio_mmio.h"
#include "platform.h"
#include "printk.h"

// Initialize MMIO transport
int virtio_mmio_init(virtio_mmio_transport_t *mmio, void *base_addr) {
  mmio->base = (volatile uint8_t *)base_addr;

  // Verify magic value
  uint32_t magic = platform_mmio_read32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_MAGIC_VALUE));
  if (magic != VIRTIO_MMIO_MAGIC) {
    return -1;
  }

  // Read version
  mmio->version = platform_mmio_read32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_VERSION));
  if (mmio->version < 1 || mmio->version > 2) {
    return -1;
  }

  return 0;
}

// Reset device
void virtio_mmio_reset(virtio_mmio_transport_t *mmio) {
  platform_mmio_write32((volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_STATUS),
                        0);
}

// Set device status
void virtio_mmio_set_status(virtio_mmio_transport_t *mmio, uint8_t status) {
  platform_mmio_write32((volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_STATUS),
                        status);
}

// Get device status
uint8_t virtio_mmio_get_status(virtio_mmio_transport_t *mmio) {
  return (uint8_t)platform_mmio_read32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_STATUS));
}

// Get device ID
uint32_t virtio_mmio_get_device_id(virtio_mmio_transport_t *mmio) {
  return platform_mmio_read32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_DEVICE_ID));
}

// Get device features
uint32_t virtio_mmio_get_features(virtio_mmio_transport_t *mmio,
                                  uint32_t select) {
  platform_mmio_write32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_DEVICE_FEATURES_SEL),
      select);
  return platform_mmio_read32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_DEVICE_FEATURES));
}

// Set driver features
void virtio_mmio_set_features(virtio_mmio_transport_t *mmio, uint32_t select,
                              uint32_t features) {
  platform_mmio_write32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_DRIVER_FEATURES_SEL),
      select);
  platform_mmio_write32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_DRIVER_FEATURES),
      features);
}

// Get queue size
uint16_t virtio_mmio_get_queue_size(virtio_mmio_transport_t *mmio,
                                    uint16_t queue_idx) {
  platform_mmio_write32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_SEL), queue_idx);
  return (uint16_t)platform_mmio_read32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_NUM_MAX));
}

// Setup queue
int virtio_mmio_setup_queue(virtio_mmio_transport_t *mmio, uint16_t queue_idx,
                            virtqueue_t *vq, uint16_t queue_size) {
  platform_mmio_write32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_SEL), queue_idx);

  // For modern devices, verify queue is not already in use (spec 4.2.3.2)
  if (mmio->version >= 2) {
    uint32_t ready = platform_mmio_read32(
        (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_READY));
    if (ready != 0) {
      return -1; // Queue already in use
    }
  }

  platform_mmio_write32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_NUM), queue_size);

  if (mmio->version == 1) {
    // Legacy (version 1): Use QUEUE_PFN with page alignment
    // IMPORTANT: Use descriptor table address, not virtqueue struct address!
    uint64_t queue_addr = (uint64_t)vq->desc;
    uint32_t pfn = (uint32_t)(queue_addr >> 12);

    platform_mmio_write32(
        (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_ALIGN), 4096);
    platform_mmio_write32(
        (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_PFN), pfn);
  } else {
    // Modern (version 2+): Use separate address registers
    uint64_t desc_addr = (uint64_t)vq->desc;
    platform_mmio_write32(
        (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_DESC_LOW),
        (uint32_t)desc_addr);
    platform_mmio_write32(
        (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_DESC_HIGH),
        (uint32_t)(desc_addr >> 32));

    uint64_t avail_addr = (uint64_t)vq->avail;
    platform_mmio_write32(
        (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_DRIVER_LOW),
        (uint32_t)avail_addr);
    platform_mmio_write32(
        (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_DRIVER_HIGH),
        (uint32_t)(avail_addr >> 32));

    uint64_t used_addr = (uint64_t)vq->used;
    platform_mmio_write32(
        (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_DEVICE_LOW),
        (uint32_t)used_addr);
    platform_mmio_write32(
        (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_DEVICE_HIGH),
        (uint32_t)(used_addr >> 32));

    platform_mmio_write32(
        (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_READY), 1);
  }

  return 0;
}

// Notify queue (kick device)
void virtio_mmio_notify_queue(virtio_mmio_transport_t *mmio,
                              uint16_t queue_idx) {
  platform_mmio_write32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_QUEUE_NOTIFY), queue_idx);
}

// Read ISR status
uint32_t virtio_mmio_read_isr(virtio_mmio_transport_t *mmio) {
  return platform_mmio_read32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_INTERRUPT_STATUS));
}

// Acknowledge ISR status
void virtio_mmio_ack_isr(virtio_mmio_transport_t *mmio, uint32_t status) {
  platform_mmio_write32(
      (volatile uint32_t *)(void *)(mmio->base + VIRTIO_MMIO_INTERRUPT_ACK), status);
}

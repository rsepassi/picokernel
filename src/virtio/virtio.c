// VirtIO Core Implementation
// Virtqueue management functions

#include "virtio.h"
#include <stddef.h>

// Helper: Calculate virtqueue layout sizes
#define VIRTQ_ALIGN(x) (((x) + 4095) & ~4095)

// Initialize virtqueue
void virtqueue_init(virtqueue_t *vq, uint16_t queue_size, void *base) {
  vq->queue_size = queue_size;
  vq->num_free = queue_size;
  vq->free_head = 0;
  vq->last_used_idx = 0;
  vq->notify_offset = 0;

  // Layout: desc | avail | used
  // desc: queue_size * sizeof(virtq_desc_t)
  // avail: 4 + queue_size * 2 + 2
  // used: 4 + queue_size * sizeof(virtq_used_elem_t) + 2

  uint8_t *ptr = (uint8_t *)base;

  // Descriptor table
  vq->desc = (virtq_desc_t *)(void *)ptr;
  ptr += queue_size * sizeof(virtq_desc_t);

  // Available ring
  vq->avail = (virtq_avail_t *)(void *)ptr;
  ptr += 4 + queue_size * 2 + 2;

  // Align used ring to 4K boundary
  ptr = (uint8_t *)VIRTQ_ALIGN((uintptr_t)ptr);

  // Used ring
  vq->used = (virtq_used_t *)(void *)ptr;

  // Initialize descriptor free list
  for (uint16_t i = 0; i < queue_size - 1; i++) {
    vq->desc[i].next = i + 1;
    vq->desc[i].flags = 0;
  }
  vq->desc[queue_size - 1].next = VIRTQUEUE_NO_DESC;
  vq->desc[queue_size - 1].flags = 0;

  // Initialize rings
  // avail->flags: bit 0 (VIRTQ_AVAIL_F_NO_INTERRUPT) MUST be 0 to receive
  // interrupts
  vq->avail->flags = 0; // We WANT interrupts
  vq->avail->idx = 0;

  // used->flags: bit 0 (VIRTQ_USED_F_NO_NOTIFY) - device tells us if it wants
  // kicks
  vq->used->flags = 0;
  vq->used->idx = 0;
}

// Allocate a descriptor
uint16_t virtqueue_alloc_desc(virtqueue_t *vq) {
  if (vq->num_free == 0) {
    return VIRTQUEUE_NO_DESC;
  }

  uint16_t desc_idx = vq->free_head;
  vq->free_head = vq->desc[desc_idx].next;
  vq->num_free--;

  return desc_idx;
}

// Setup descriptor
void virtqueue_add_desc(virtqueue_t *vq, uint16_t idx, uint64_t addr,
                        uint32_t len, uint16_t flags) {
  vq->desc[idx].addr = addr;
  vq->desc[idx].len = len;
  vq->desc[idx].flags = flags;
  vq->desc[idx].next = VIRTQUEUE_NO_DESC;
}

// Add descriptor chain to available ring
void virtqueue_add_avail(virtqueue_t *vq, uint16_t desc_idx) {
  uint16_t avail_idx = vq->avail->idx % vq->queue_size;
  vq->avail->ring[avail_idx] = desc_idx;

  // Memory barrier before updating idx
  __sync_synchronize();

  vq->avail->idx++;
}

// Kick device (notify new descriptors available)
void virtqueue_kick(virtqueue_t *vq) {
  (void)vq; // Unused - actual notification happens in platform layer

  // Memory barrier before notify
  __sync_synchronize();

  // Platform-specific notification (implemented by platform code)
  // This is just a placeholder - actual notification happens in platform layer
}

// Check if used ring has entries
int virtqueue_has_used(virtqueue_t *vq) {
  return vq->last_used_idx != vq->used->idx;
}

// Get used descriptor
void virtqueue_get_used(virtqueue_t *vq, uint16_t *desc_idx, uint32_t *len) {
  uint16_t used_idx = vq->last_used_idx % vq->queue_size;
  *desc_idx = vq->used->ring[used_idx].id;
  *len = vq->used->ring[used_idx].len;

  vq->last_used_idx++;
}

// Free descriptor
void virtqueue_free_desc(virtqueue_t *vq, uint16_t desc_idx) {
  vq->desc[desc_idx].next = vq->free_head;
  vq->free_head = desc_idx;
  vq->num_free++;
}

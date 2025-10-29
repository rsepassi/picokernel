// VirtIO Core Structures
// Platform-agnostic VirtIO protocol (virtqueue, descriptors, device logic)

#pragma once

#include <stddef.h>
#include <stdint.h>

// Endianness types (VirtIO spec requires little-endian)
// Current implementation assumes little-endian architecture (x64, ARM64)
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#error "Big-endian architectures not supported - byte swapping required"
#endif

typedef uint16_t le16;
typedef uint32_t le32;
typedef uint64_t le64;

// VirtIO descriptor flags
#define VIRTQ_DESC_F_NEXT 1     // Descriptor has next
#define VIRTQ_DESC_F_WRITE 2    // Device writes (vs read)
#define VIRTQ_DESC_F_INDIRECT 4 // Buffer contains descriptor list

// Ring notification flags
#define VIRTQ_USED_F_NO_NOTIFY                                                 \
  1 // Device advises: don't kick when adding buffer
#define VIRTQ_AVAIL_F_NO_INTERRUPT                                             \
  1 // Driver advises: don't interrupt when consuming

// VirtIO feature flags (for feature negotiation)
#define VIRTIO_F_ANY_LAYOUT 27    // Arbitrary descriptor layouts
#define VIRTIO_F_INDIRECT_DESC 28 // Support for indirect descriptors
#define VIRTIO_F_EVENT_IDX 29 // Support for avail_event and used_event fields

// Special value for no descriptor
#define VIRTQUEUE_NO_DESC 0xFFFF

// VirtIO descriptor
typedef struct {
  uint64_t addr;  // Physical address
  uint32_t len;   // Length
  uint16_t flags; // VIRTQ_DESC_F_*
  uint16_t next;  // Next descriptor (if NEXT flag set)
} virtq_desc_t;

// Available ring
typedef struct {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[]; // Variable size
} virtq_avail_t;

// Used ring entry
typedef struct {
  uint32_t id;  // Descriptor chain head
  uint32_t len; // Bytes written
} virtq_used_elem_t;

// Used ring
typedef struct {
  uint16_t flags;
  uint16_t idx;
  virtq_used_elem_t ring[]; // Variable size
} virtq_used_t;

// Virtqueue management structure
typedef struct {
  uint16_t queue_size;
  uint16_t num_free;
  uint16_t free_head;     // Free list head
  uint16_t last_used_idx; // Last processed used index

  virtq_desc_t *desc;   // Descriptor table
  virtq_avail_t *avail; // Available ring
  virtq_used_t *used;   // Used ring

  // PCI notify offset (platform-specific)
  uint16_t notify_offset;
} virtqueue_t;

// Initialize virtqueue
void virtqueue_init(virtqueue_t *vq, uint16_t queue_size, void *base);

// Allocate a descriptor (returns index or VIRTQUEUE_NO_DESC if full)
uint16_t virtqueue_alloc_desc(virtqueue_t *vq);

// Setup descriptor
void virtqueue_add_desc(virtqueue_t *vq, uint16_t idx, uint64_t addr,
                        uint32_t len, uint16_t flags);

// Add descriptor chain to available ring
void virtqueue_add_avail(virtqueue_t *vq, uint16_t desc_idx);

// Kick device (notify new descriptors available)
void virtqueue_kick(virtqueue_t *vq);

// Check if used ring has entries
int virtqueue_has_used(virtqueue_t *vq);

// Get used descriptor (returns descriptor index and length)
void virtqueue_get_used(virtqueue_t *vq, uint16_t *desc_idx, uint32_t *len);

// Free descriptor
void virtqueue_free_desc(virtqueue_t *vq, uint16_t desc_idx);

// Event index helpers (only valid when VIRTIO_F_EVENT_IDX negotiated)
// Determine if event notification is needed
static inline int virtq_need_event(uint16_t event_idx, uint16_t new_idx,
                                   uint16_t old_idx) {
  return (uint16_t)(new_idx - event_idx - 1) < (uint16_t)(new_idx - old_idx);
}

// Get location of used_event index (at end of avail ring)
static inline le16 *virtq_used_event(virtqueue_t *vq) {
  return &vq->avail->ring[vq->queue_size];
}

// Get location of avail_event index (at end of used ring)
static inline le16 *virtq_avail_event(virtqueue_t *vq) {
  return (le16 *)&vq->used->ring[vq->queue_size];
}

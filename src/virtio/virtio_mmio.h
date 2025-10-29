// Generic VirtIO MMIO Transport
// Platform-agnostic MMIO transport implementation

#pragma once

#include "virtio.h"
#include <stdint.h>

// VirtIO device status bits
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED 128

// VirtIO MMIO register offsets (generic to MMIO spec)
#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028 // V1 only (legacy)
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
// Version 1 (legacy) registers
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03c // V1 only
#define VIRTIO_MMIO_QUEUE_PFN 0x040   // V1 only (Page Frame Number)
// Version 2+ registers
#define VIRTIO_MMIO_QUEUE_READY 0x044 // V2+ only
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064
#define VIRTIO_MMIO_STATUS 0x070
// Version 2+ queue address registers
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080    // V2+ only
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084   // V2+ only
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW 0x090  // V2+ only
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH 0x094 // V2+ only
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW 0x0a0  // V2+ only
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH 0x0a4 // V2+ only

// VirtIO device IDs
#define VIRTIO_ID_RNG 4
#define VIRTIO_ID_BLOCK 2
#define VIRTIO_ID_NET 1

// VirtIO magic value for MMIO
#define VIRTIO_MMIO_MAGIC 0x74726976 // "virt" in little-endian

// VirtIO MMIO transport state
typedef struct {
  volatile uint8_t *base; // MMIO base address
  uint32_t version;       // 1 = legacy, 2 = modern
} virtio_mmio_transport_t;

// Initialize MMIO transport
// Returns 0 on success, -1 on failure
int virtio_mmio_init(virtio_mmio_transport_t *mmio, void *base_addr);

// Reset device
void virtio_mmio_reset(virtio_mmio_transport_t *mmio);

// Get/set device status
void virtio_mmio_set_status(virtio_mmio_transport_t *mmio, uint8_t status);
uint8_t virtio_mmio_get_status(virtio_mmio_transport_t *mmio);

// Read device ID
uint32_t virtio_mmio_get_device_id(virtio_mmio_transport_t *mmio);

// Feature negotiation
uint32_t virtio_mmio_get_features(virtio_mmio_transport_t *mmio,
                                  uint32_t select);
void virtio_mmio_set_features(virtio_mmio_transport_t *mmio, uint32_t select,
                              uint32_t features);

// Queue operations
uint16_t virtio_mmio_get_queue_size(virtio_mmio_transport_t *mmio,
                                    uint16_t queue_idx);
int virtio_mmio_setup_queue(virtio_mmio_transport_t *mmio, uint16_t queue_idx,
                            virtqueue_t *vq, uint16_t queue_size);
void virtio_mmio_notify_queue(virtio_mmio_transport_t *mmio,
                              uint16_t queue_idx);

// ISR status
uint32_t virtio_mmio_read_isr(virtio_mmio_transport_t *mmio);
void virtio_mmio_ack_isr(virtio_mmio_transport_t *mmio, uint32_t status);

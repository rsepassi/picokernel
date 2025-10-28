// ARM64 VirtIO MMIO Transport Layer
// VirtIO device structures and MMIO-specific operations

#pragma once

#include "kernel.h"
#include "virtio/virtio.h"

// VirtIO device status bits
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED 128

// VirtIO MMIO register offsets
#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
// Version 1 (legacy) register
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

// VirtIO magic value for MMIO
#define VIRTIO_MMIO_MAGIC 0x74726976 // "virt" in little-endian

// Initialize VirtIO-RNG device via MMIO
// (virtio_rng_t is defined in platform_impl.h)
void virtio_rng_mmio_setup(platform_t *platform, uint64_t mmio_base,
                           uint64_t mmio_size, uint32_t irq_num);

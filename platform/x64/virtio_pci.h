// x64 VirtIO PCI Transport Layer
// VirtIO device structures and PCI-specific operations

#pragma once

#include "virtio/virtio.h"
#include "kernel.h"

// VirtIO device status bits
#define VIRTIO_STATUS_ACKNOWLEDGE  1
#define VIRTIO_STATUS_DRIVER       2
#define VIRTIO_STATUS_DRIVER_OK    4
#define VIRTIO_STATUS_FEATURES_OK  8
#define VIRTIO_STATUS_FAILED       128

// VirtIO PCI capability types
#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4
#define VIRTIO_PCI_CAP_PCI_CFG      5

// VirtIO PCI capability structure
typedef struct {
    uint8_t cap_vndr;    // Generic PCI field: 0x09 = vendor-specific
    uint8_t cap_next;    // Next capability offset
    uint8_t cap_len;     // Capability length
    uint8_t cfg_type;    // VIRTIO_PCI_CAP_*
    uint8_t bar;         // BAR index
    uint8_t padding[3];
    uint32_t offset;     // Offset within BAR
    uint32_t length;     // Length of structure
} __attribute__((packed)) virtio_pci_cap_t;

// VirtIO notify capability (extends virtio_pci_cap_t)
typedef struct {
    virtio_pci_cap_t cap;
    uint32_t notify_off_multiplier;
} __attribute__((packed)) virtio_pci_notify_cap_t;

// VirtIO common config (MMIO structure)
typedef struct {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_driver;
    uint64_t queue_device;
} __attribute__((packed)) virtio_pci_common_cfg_t;

// VirtIO-RNG device state
#define VIRTIO_RNG_MAX_REQUESTS 256

typedef struct virtio_rng_t {
    // PCI location
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_func;

    // BARs (physical addresses)
    uint64_t common_cfg_bar;     // Common config BAR base
    uint64_t notify_bar;         // Notification BAR base
    uint64_t isr_bar;            // ISR status BAR base

    // MMIO pointers
    volatile virtio_pci_common_cfg_t* common_cfg;
    volatile uint32_t* notify_base;
    volatile uint8_t* isr_status;  // ISR status is a single byte register

    // Capability offsets
    uint32_t common_cfg_offset;
    uint32_t notify_offset;
    uint32_t isr_offset;
    uint32_t notify_off_multiplier;

    // Virtqueue
    virtqueue_t vq;
    uint16_t queue_size;
    void* vq_memory;  // Allocated memory for virtqueue

    // IRQ routing
    uint8_t irq_vector;

    // Request tracking (constant-time lookup)
    krng_req_t* active_requests[VIRTIO_RNG_MAX_REQUESTS];

    // Interrupt pending flag (set by ISR, cleared by ktick)
    volatile uint8_t irq_pending;

    // Back-pointer to kernel
    kernel_t* kernel;
} virtio_rng_t;

// Scan PCI bus for devices
void pci_scan_devices(platform_t* platform);

void virtio_rng_setup(platform_t* platform, uint8_t bus, uint8_t slot, uint8_t func);

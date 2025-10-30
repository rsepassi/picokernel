// Generic VirtIO PCI Transport
// Platform-agnostic PCI transport implementation

#pragma once

#include "virtio.h"

// VirtIO device status bits
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED 128

// VirtIO PCI capability types
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG 3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4
#define VIRTIO_PCI_CAP_PCI_CFG 5

// VirtIO common config (MMIO structure in PCI BAR space)
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

// Forward declaration
struct platform_t;

// VirtIO PCI transport state
typedef struct {
  // Platform pointer (for PCI config space access)
  struct platform_t *platform;

  // PCI location
  uint8_t bus, slot, func;

  // Capability structures (mapped into BAR space)
  volatile virtio_pci_common_cfg_t *common_cfg;
  volatile uint8_t *isr_status;
  volatile void *device_cfg; // Device-specific configuration
  uint64_t notify_base;
  uint32_t notify_off_multiplier;
} virtio_pci_transport_t;

// Initialize PCI transport (parses capabilities)
// Returns 0 on success, -1 on failure
int virtio_pci_init(virtio_pci_transport_t *pci, struct platform_t *platform,
                    uint8_t bus, uint8_t slot, uint8_t func);

// Device control
void virtio_pci_reset(virtio_pci_transport_t *pci);
void virtio_pci_set_status(virtio_pci_transport_t *pci, uint8_t status);
uint8_t virtio_pci_get_status(virtio_pci_transport_t *pci);

// Feature negotiation
uint32_t virtio_pci_get_features(virtio_pci_transport_t *pci, uint32_t select);
void virtio_pci_set_features(virtio_pci_transport_t *pci, uint32_t select,
                             uint32_t features);

// Queue operations
int virtio_pci_setup_queue(virtio_pci_transport_t *pci, uint16_t queue_idx,
                           virtqueue_t *vq, uint16_t queue_size);
void virtio_pci_notify_queue(virtio_pci_transport_t *pci, uint16_t queue_idx);

// ISR status
uint8_t virtio_pci_read_isr(virtio_pci_transport_t *pci);

// Get queue size
uint16_t virtio_pci_get_queue_size(virtio_pci_transport_t *pci,
                                   uint16_t queue_idx);

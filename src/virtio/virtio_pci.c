// Generic VirtIO PCI Transport Implementation
// Platform-agnostic PCI transport using platform hooks

#include "virtio_pci.h"
#include "kbase.h"
#include "platform.h"

// PCI config space register offsets
#define PCI_REG_COMMAND 0x04
#define PCI_REG_CAPABILITIES 0x34
#define PCI_CMD_MEM_ENABLE (1 << 1)
#define PCI_CMD_BUS_MASTER (1 << 2)
#define PCI_CMD_INT_DISABLE (1 << 10)

// MMIO helpers using platform hooks
static inline uint32_t mmio_read32(volatile uint32_t *addr) { return *addr; }

static inline uint16_t mmio_read16(volatile uint16_t *addr) { return *addr; }

static inline uint8_t mmio_read8(volatile uint8_t *addr) { return *addr; }

static inline void mmio_write32(volatile uint32_t *addr, uint32_t value) {
  *addr = value;
}

static inline void mmio_write16(volatile uint16_t *addr, uint16_t value) {
  *addr = value;
}

static inline void mmio_write8(volatile uint8_t *addr, uint8_t value) {
  *addr = value;
}

static inline void mmio_write64(volatile uint64_t *addr, uint64_t value) {
  memcpy((void *)addr, &value, sizeof(value));
}

// Find and map VirtIO PCI capabilities
static int virtio_find_capabilities(virtio_pci_transport_t *pci) {
  uint8_t cap_offset = platform_pci_config_read8(
      pci->platform, pci->bus, pci->slot, pci->func, PCI_REG_CAPABILITIES);

  if (cap_offset == 0) {
    return -1; // No capabilities
  }

  int found_common = 0, found_notify = 0, found_isr = 0, found_device = 0;

  while (cap_offset != 0) {
    uint8_t cap_id = platform_pci_config_read8(
        pci->platform, pci->bus, pci->slot, pci->func, cap_offset);

    if (cap_id == 0x09) { // Vendor-specific capability
      uint8_t cfg_type = platform_pci_config_read8(
          pci->platform, pci->bus, pci->slot, pci->func, cap_offset + 3);
      uint8_t bar = platform_pci_config_read8(
          pci->platform, pci->bus, pci->slot, pci->func, cap_offset + 4);
      uint32_t offset = platform_pci_config_read32(
          pci->platform, pci->bus, pci->slot, pci->func, cap_offset + 8);

      uint64_t bar_base = platform_pci_read_bar(pci->platform, pci->bus,
                                                pci->slot, pci->func, bar);

      if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
        pci->common_cfg =
            (volatile virtio_pci_common_cfg_t *)(bar_base + offset);
        found_common = 1;
      } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
        pci->notify_base = bar_base + offset;
        pci->notify_off_multiplier = platform_pci_config_read32(
            pci->platform, pci->bus, pci->slot, pci->func, cap_offset + 16);
        found_notify = 1;
      } else if (cfg_type == VIRTIO_PCI_CAP_ISR_CFG) {
        pci->isr_status = (volatile uint8_t *)(bar_base + offset);
        found_isr = 1;
      } else if (cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
        pci->device_cfg = (volatile void *)(bar_base + offset);
        found_device = 1;
      }
    }

    // Next capability
    cap_offset = platform_pci_config_read8(pci->platform, pci->bus, pci->slot,
                                           pci->func, cap_offset + 1);
  }

  // Device config is optional (RNG doesn't have it)
  (void)found_device;
  return (found_common && found_notify && found_isr) ? 0 : -1;
}

// Initialize PCI transport
int virtio_pci_init(virtio_pci_transport_t *pci, platform_t *platform,
                    uint8_t bus, uint8_t slot, uint8_t func) {
  pci->platform = platform;
  pci->bus = bus;
  pci->slot = slot;
  pci->func = func;

  // Enable PCI bus mastering and memory access, disable interrupt masking
  uint16_t command =
      platform_pci_config_read16(platform, bus, slot, func, PCI_REG_COMMAND);
  command |= PCI_CMD_MEM_ENABLE | PCI_CMD_BUS_MASTER;
  command &= ~PCI_CMD_INT_DISABLE;
  platform_pci_config_write16(platform, bus, slot, func, PCI_REG_COMMAND,
                              command);

  // Find and map capabilities
  if (virtio_find_capabilities(pci) < 0) {
    return -1;
  }

  return 0;
}

// Reset device
void virtio_pci_reset(virtio_pci_transport_t *pci) {
  mmio_write8((volatile uint8_t *)&pci->common_cfg->device_status, 0);
}

// Set device status
void virtio_pci_set_status(virtio_pci_transport_t *pci, uint8_t status) {
  mmio_write8((volatile uint8_t *)&pci->common_cfg->device_status, status);
}

// Get device status
uint8_t virtio_pci_get_status(virtio_pci_transport_t *pci) {
  return mmio_read8((volatile uint8_t *)&pci->common_cfg->device_status);
}

// Get device features
uint32_t virtio_pci_get_features(virtio_pci_transport_t *pci, uint32_t select) {
  mmio_write32(&pci->common_cfg->device_feature_select, select);
  return mmio_read32(&pci->common_cfg->device_feature);
}

// Set driver features
void virtio_pci_set_features(virtio_pci_transport_t *pci, uint32_t select,
                             uint32_t features) {
  mmio_write32(&pci->common_cfg->driver_feature_select, select);
  mmio_write32(&pci->common_cfg->driver_feature, features);
}

// Get queue size
uint16_t virtio_pci_get_queue_size(virtio_pci_transport_t *pci,
                                   uint16_t queue_idx) {
  mmio_write16(&pci->common_cfg->queue_select, queue_idx);
  return mmio_read16(&pci->common_cfg->queue_size);
}

// Setup queue
int virtio_pci_setup_queue(virtio_pci_transport_t *pci, uint16_t queue_idx,
                           virtqueue_t *vq, uint16_t queue_size) {
  (void)queue_size; // Unused parameter
  // Select queue
  mmio_write16(&pci->common_cfg->queue_select, queue_idx);

  // Set queue addresses
  mmio_write64(&pci->common_cfg->queue_desc, (uint64_t)vq->desc);
  mmio_write64(&pci->common_cfg->queue_driver, (uint64_t)vq->avail);
  mmio_write64(&pci->common_cfg->queue_device, (uint64_t)vq->used);

  // Store queue index for notifications
  vq->queue_index = queue_idx;

  // Get notify offset for this queue
  uint16_t notify_off = mmio_read16(&pci->common_cfg->queue_notify_off);
  vq->notify_offset = notify_off;

  // Disable MSI-X for this queue (use legacy interrupts)
  mmio_write16(&pci->common_cfg->queue_msix_vector, 0xFFFF);

  // Enable queue
  mmio_write16(&pci->common_cfg->queue_enable, 1);

  return 0;
}

// Notify queue (kick device)
void virtio_pci_notify_queue(virtio_pci_transport_t *pci, virtqueue_t *vq) {
  // Calculate notify address using the queue's notify_offset
  // This was stored during virtio_pci_setup_queue()
  uint64_t notify_addr =
      pci->notify_base + (pci->notify_off_multiplier * vq->notify_offset);
  volatile uint16_t *notify_ptr = (volatile uint16_t *)notify_addr;

  // Write queue index to notify register
  mmio_write16(notify_ptr, vq->queue_index);
}

// Read ISR status
uint8_t virtio_pci_read_isr(virtio_pci_transport_t *pci) {
  return mmio_read8(pci->isr_status);
}

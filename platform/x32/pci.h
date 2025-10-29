// x32 PCI Configuration Space Access
// Standard PCI bus enumeration and config space I/O

#pragma once

#include "platform.h"

// PCI config space registers
#define PCI_REG_VENDOR_ID 0x00
#define PCI_REG_DEVICE_ID 0x02
#define PCI_REG_COMMAND 0x04
#define PCI_REG_STATUS 0x06
#define PCI_REG_REVISION_ID 0x08
#define PCI_REG_CLASS_CODE 0x09
#define PCI_REG_HEADER_TYPE 0x0E
#define PCI_REG_BAR0 0x10
#define PCI_REG_BAR1 0x14
#define PCI_REG_BAR2 0x18
#define PCI_REG_BAR3 0x1C
#define PCI_REG_BAR4 0x20
#define PCI_REG_BAR5 0x24
#define PCI_REG_CAPABILITIES 0x34
#define PCI_REG_INTERRUPT_LINE 0x3C
#define PCI_REG_INTERRUPT_PIN 0x3D

// PCI command register bits
#define PCI_CMD_IO_ENABLE (1 << 0)
#define PCI_CMD_MEM_ENABLE (1 << 1)
#define PCI_CMD_BUS_MASTER (1 << 2)
#define PCI_CMD_INT_DISABLE (1 << 10)

// VirtIO PCI vendor/device IDs
#define VIRTIO_PCI_VENDOR_ID 0x1AF4

// VirtIO legacy device IDs (Transitional devices: 0x1000 + device type)
#define VIRTIO_PCI_DEVICE_NET_LEGACY 0x1000
#define VIRTIO_PCI_DEVICE_BLOCK_LEGACY 0x1001
#define VIRTIO_PCI_DEVICE_RNG_LEGACY 0x1005

// VirtIO modern device IDs (Non-transitional devices: 0x1040 + device type)
#define VIRTIO_PCI_DEVICE_NET_MODERN 0x1041
#define VIRTIO_PCI_DEVICE_BLOCK_MODERN 0x1042
#define VIRTIO_PCI_DEVICE_RNG_MODERN 0x1044

// PCI BAR types
#define PCI_BAR_TYPE_MMIO 0
#define PCI_BAR_TYPE_IO 1

// x32 implements the PCI platform interface (see pci_platform.h)
// Additional x32-specific PCI functions:

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
                        uint32_t value);

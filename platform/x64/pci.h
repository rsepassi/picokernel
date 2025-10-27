// x64 PCI Configuration Space Access
// Standard PCI bus enumeration and config space I/O

#pragma once

#include <stdint.h>

// PCI config space registers
#define PCI_REG_VENDOR_ID      0x00
#define PCI_REG_DEVICE_ID      0x02
#define PCI_REG_COMMAND        0x04
#define PCI_REG_STATUS         0x06
#define PCI_REG_REVISION_ID    0x08
#define PCI_REG_CLASS_CODE     0x09
#define PCI_REG_HEADER_TYPE    0x0E
#define PCI_REG_BAR0           0x10
#define PCI_REG_BAR1           0x14
#define PCI_REG_BAR2           0x18
#define PCI_REG_BAR3           0x1C
#define PCI_REG_BAR4           0x20
#define PCI_REG_BAR5           0x24
#define PCI_REG_CAPABILITIES   0x34
#define PCI_REG_INTERRUPT_LINE 0x3C
#define PCI_REG_INTERRUPT_PIN  0x3D

// PCI command register bits
#define PCI_CMD_IO_ENABLE      (1 << 0)
#define PCI_CMD_MEM_ENABLE     (1 << 1)
#define PCI_CMD_BUS_MASTER     (1 << 2)
#define PCI_CMD_INT_DISABLE    (1 << 10)

// VirtIO PCI vendor/device IDs
#define VIRTIO_PCI_VENDOR_ID   0x1AF4
#define VIRTIO_PCI_DEVICE_RNG_LEGACY  0x1005
#define VIRTIO_PCI_DEVICE_RNG_MODERN  0x1044

// PCI BAR types
#define PCI_BAR_TYPE_MMIO      0
#define PCI_BAR_TYPE_IO        1

// Read/write PCI config space
uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

// Read BAR address (returns 0 if BAR is not present)
uint64_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_num);

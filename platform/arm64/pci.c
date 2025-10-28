// ARM64 PCI Stubs
// ARM64 currently does not support PCI, but stubs are provided
// so that generic VirtIO PCI code can link

#include "pci.h"

// PCI config space access stubs
// These are never called at runtime since ARM64 only discovers MMIO devices
uint8_t platform_pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func,
                                  uint8_t offset) {
  (void)bus;
  (void)slot;
  (void)func;
  (void)offset;
  return 0xFF;
}

uint16_t platform_pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func,
                                    uint8_t offset) {
  (void)bus;
  (void)slot;
  (void)func;
  (void)offset;
  return 0xFFFF;
}

uint32_t platform_pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func,
                                    uint8_t offset) {
  (void)bus;
  (void)slot;
  (void)func;
  (void)offset;
  return 0xFFFFFFFF;
}

void platform_pci_config_write8(uint8_t bus, uint8_t slot, uint8_t func,
                                uint8_t offset, uint8_t value) {
  (void)bus;
  (void)slot;
  (void)func;
  (void)offset;
  (void)value;
}

void platform_pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func,
                                 uint8_t offset, uint16_t value) {
  (void)bus;
  (void)slot;
  (void)func;
  (void)offset;
  (void)value;
}

uint64_t platform_pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func,
                               uint8_t bar_num) {
  (void)bus;
  (void)slot;
  (void)func;
  (void)bar_num;
  return 0;
}

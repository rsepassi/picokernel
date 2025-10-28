// PCI Platform Interface
// Defines the PCI configuration space access interface that platforms must provide
// Similar to platform_hooks.h, this defines the contract between generic VirtIO PCI
// code and platform-specific PCI implementations

#pragma once

#include <stdint.h>

// PCI Configuration Space Access
// Each platform (x64, arm64 with PCIe, rv64 with PCIe) must implement these functions
// Platforms without PCI support should provide stubs that return error values

// Read from PCI configuration space
uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func,
                         uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func,
                           uint8_t offset);
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func,
                           uint8_t offset);

// Write to PCI configuration space
void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
                       uint8_t value);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func,
                        uint8_t offset, uint16_t value);

// Read Base Address Register (BAR)
// Returns the physical address mapped by the BAR, or 0 if BAR is not present
uint64_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_num);

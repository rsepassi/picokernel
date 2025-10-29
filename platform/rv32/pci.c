// RV32 PCI Configuration Space Access Implementation
// Uses ECAM (Enhanced Configuration Access Mechanism) - memory-mapped PCI

#include "pci.h"
#include <stdint.h>

// ECAM base address for QEMU virt machine
// This is the standard base address used by QEMU's RISC-V virt platform
#define PCI_ECAM_BASE 0x30000000UL

// Calculate ECAM address for PCI configuration space access
// ECAM layout: [bus:8][device:5][function:3][offset:12]
static inline volatile void *pci_ecam_address(uint8_t bus, uint8_t slot,
                                              uint8_t func, uint8_t offset) {
  uint32_t addr = PCI_ECAM_BASE | ((uint32_t)bus << 20) |
                  ((uint32_t)slot << 15) | ((uint32_t)func << 12) |
                  (uint32_t)offset;
  return (volatile void *)addr;
}

// Read 8-bit value from PCI config space
uint8_t platform_pci_config_read8(platform_t *platform, uint8_t bus,
                                  uint8_t slot, uint8_t func, uint8_t offset) {
  (void)platform; // Unused on RV32
  volatile uint8_t *addr =
      (volatile uint8_t *)pci_ecam_address(bus, slot, func, offset);
  return *addr;
}

// Read 16-bit value from PCI config space
uint16_t platform_pci_config_read16(platform_t *platform, uint8_t bus,
                                    uint8_t slot, uint8_t func, uint8_t offset) {
  (void)platform; // Unused on RV32
  volatile uint16_t *addr =
      (volatile uint16_t *)pci_ecam_address(bus, slot, func, offset);
  return *addr;
}

// Read 32-bit value from PCI config space
uint32_t platform_pci_config_read32(platform_t *platform, uint8_t bus,
                                    uint8_t slot, uint8_t func, uint8_t offset) {
  (void)platform; // Unused on RV32
  volatile uint32_t *addr =
      (volatile uint32_t *)pci_ecam_address(bus, slot, func, offset);
  return *addr;
}

// Write 8-bit value to PCI config space
void platform_pci_config_write8(platform_t *platform, uint8_t bus,
                                uint8_t slot, uint8_t func, uint8_t offset,
                                uint8_t value) {
  (void)platform; // Unused on RV32
  volatile uint8_t *addr =
      (volatile uint8_t *)pci_ecam_address(bus, slot, func, offset);
  *addr = value;
}

// Write 16-bit value to PCI config space
void platform_pci_config_write16(platform_t *platform, uint8_t bus,
                                 uint8_t slot, uint8_t func, uint8_t offset,
                                 uint16_t value) {
  (void)platform; // Unused on RV32
  volatile uint16_t *addr =
      (volatile uint16_t *)pci_ecam_address(bus, slot, func, offset);
  *addr = value;
}

// Write 32-bit value to PCI config space
void platform_pci_config_write32(platform_t *platform, uint8_t bus,
                                 uint8_t slot, uint8_t func, uint8_t offset,
                                 uint32_t value) {
  (void)platform; // Unused on RV32
  volatile uint32_t *addr =
      (volatile uint32_t *)pci_ecam_address(bus, slot, func, offset);
  *addr = value;
}

// Read BAR address
uint64_t platform_pci_read_bar(platform_t *platform, uint8_t bus, uint8_t slot,
                               uint8_t func, uint8_t bar_num) {
  if (bar_num > 5) {
    return 0;
  }

  uint8_t bar_offset = PCI_REG_BAR0 + (bar_num * 4);
  uint32_t bar_low =
      platform_pci_config_read32(platform, bus, slot, func, bar_offset);

  if (bar_low == 0 || bar_low == 0xFFFFFFFF) {
    return 0; // BAR not present
  }

  // Check if I/O space BAR (bit 0 set)
  if (bar_low & 0x1) {
    // I/O space - return 0 as we don't support I/O BARs on RV32
    return 0;
  }

  // Memory space BAR - check type bits [2:1]
  uint32_t bar_type = (bar_low >> 1) & 0x3;

  if (bar_type == 0x2) {
    // 64-bit BAR
    uint32_t bar_high =
        platform_pci_config_read32(platform, bus, slot, func, bar_offset + 4);
    uint64_t addr = ((uint64_t)bar_high << 32) | (bar_low & ~0xFU);
    return addr;
  } else if (bar_type == 0x0) {
    // 32-bit BAR
    return bar_low & ~0xFU;
  } else {
    // Reserved or unsupported type
    return 0;
  }
}

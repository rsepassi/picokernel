// x32 PCI Configuration Space Access Implementation
// Uses I/O ports 0xCF8 (address) and 0xCFC (data)

#include "pci.h"
#include "io.h"
#include "platform_impl.h"

// PCI configuration space I/O ports
#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

// Build PCI configuration address
static uint32_t pci_make_address(uint8_t bus, uint8_t slot, uint8_t func,
                                 uint8_t offset) {
  return (1U << 31) | // Enable bit
         ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
         ((uint32_t)func << 8) | (offset & 0xFC); // Align to 4-byte boundary
}

// Read 8-bit value from PCI config space
uint8_t platform_pci_config_read8(platform_t *platform, uint8_t bus,
                                  uint8_t slot, uint8_t func, uint8_t offset) {
  (void)platform; // Unused on x32
  uint32_t address = pci_make_address(bus, slot, func, offset);
  outl(PCI_CONFIG_ADDR, address);
  return inb(PCI_CONFIG_DATA + (offset & 3));
}

// Read 16-bit value from PCI config space
uint16_t platform_pci_config_read16(platform_t *platform, uint8_t bus,
                                    uint8_t slot, uint8_t func, uint8_t offset) {
  (void)platform; // Unused on x32
  uint32_t address = pci_make_address(bus, slot, func, offset);
  outl(PCI_CONFIG_ADDR, address);
  return inw(PCI_CONFIG_DATA + (offset & 2));
}

// Read 32-bit value from PCI config space
uint32_t platform_pci_config_read32(platform_t *platform, uint8_t bus,
                                    uint8_t slot, uint8_t func, uint8_t offset) {
  (void)platform; // Unused on x32
  uint32_t address = pci_make_address(bus, slot, func, offset);
  outl(PCI_CONFIG_ADDR, address);
  return inl(PCI_CONFIG_DATA);
}

// Write 8-bit value to PCI config space
void platform_pci_config_write8(platform_t *platform, uint8_t bus,
                                uint8_t slot, uint8_t func, uint8_t offset,
                                uint8_t value) {
  (void)platform; // Unused on x32
  uint32_t address = pci_make_address(bus, slot, func, offset);
  outl(PCI_CONFIG_ADDR, address);
  outb(PCI_CONFIG_DATA + (offset & 3), value);
}

// Write 16-bit value to PCI config space
void platform_pci_config_write16(platform_t *platform, uint8_t bus,
                                 uint8_t slot, uint8_t func, uint8_t offset,
                                 uint16_t value) {
  (void)platform; // Unused on x32
  uint32_t address = pci_make_address(bus, slot, func, offset);
  outl(PCI_CONFIG_ADDR, address);
  outw(PCI_CONFIG_DATA + (offset & 2), value);
}

// Write 32-bit value to PCI config space
void platform_pci_config_write32(platform_t *platform, uint8_t bus,
                                 uint8_t slot, uint8_t func, uint8_t offset,
                                 uint32_t value) {
  (void)platform; // Unused on x32
  uint32_t address = pci_make_address(bus, slot, func, offset);
  outl(PCI_CONFIG_ADDR, address);
  outl(PCI_CONFIG_DATA, value);
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

  // Check if 64-bit BAR
  if ((bar_low & 0x6) == 0x4) {
    // 64-bit BAR
    uint32_t bar_high =
        platform_pci_config_read32(platform, bus, slot, func, bar_offset + 4);
    uint64_t addr = ((uint64_t)bar_high << 32) | (bar_low & ~0xFULL);
    return addr;
  } else {
    // 32-bit BAR
    return bar_low & ~0xFULL;
  }
}

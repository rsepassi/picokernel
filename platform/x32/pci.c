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
                                    uint8_t slot, uint8_t func,
                                    uint8_t offset) {
  (void)platform; // Unused on x32
  uint32_t address = pci_make_address(bus, slot, func, offset);
  outl(PCI_CONFIG_ADDR, address);
  return inw(PCI_CONFIG_DATA + (offset & 2));
}

// Read 32-bit value from PCI config space
uint32_t platform_pci_config_read32(platform_t *platform, uint8_t bus,
                                    uint8_t slot, uint8_t func,
                                    uint8_t offset) {
  (void)platform; // Unused on x32
  uint32_t address = pci_make_address(bus, slot, func, offset);
  outl(PCI_CONFIG_ADDR, address);
  return inl(PCI_CONFIG_DATA);
}

// Write 8-bit value to PCI config space
void platform_pci_config_write8(platform_t *platform, uint8_t bus, uint8_t slot,
                                uint8_t func, uint8_t offset, uint8_t value) {
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

// Find MSI-X capability in PCI config space
// Returns capability offset, or 0 if not found
uint8_t pci_find_msix_capability(platform_t *platform, uint8_t bus,
                                 uint8_t slot, uint8_t func) {
  // Check if device has capabilities
  uint16_t status =
      platform_pci_config_read16(platform, bus, slot, func, PCI_REG_STATUS);
  if (!(status & 0x10)) {
    return 0; // No capabilities list
  }

  // Walk capabilities list
  uint8_t cap_ptr =
      platform_pci_config_read8(platform, bus, slot, func, PCI_REG_CAPABILITIES);
  cap_ptr &= 0xFC; // Align to 4-byte boundary

  while (cap_ptr != 0) {
    uint8_t cap_id =
        platform_pci_config_read8(platform, bus, slot, func, cap_ptr);
    if (cap_id == PCI_CAP_ID_MSIX) {
      return cap_ptr;
    }

    // Next capability
    cap_ptr =
        platform_pci_config_read8(platform, bus, slot, func, cap_ptr + 1);
    cap_ptr &= 0xFC;
  }

  return 0; // MSI-X not found
}

// Configure an MSI-X vector
// vector_idx: Index in MSI-X table (0-based)
// cpu_vector: Interrupt vector number (e.g., 33-47)
// apic_id: Target LAPIC ID
void pci_configure_msix_vector(platform_t *platform, uint8_t bus, uint8_t slot,
                               uint8_t func, uint16_t vector_idx,
                               uint8_t cpu_vector, uint8_t apic_id) {
  // Find MSI-X capability
  uint8_t msix_cap = pci_find_msix_capability(platform, bus, slot, func);
  if (msix_cap == 0) {
    return; // MSI-X not supported
  }

  // Read MSI-X table location (BAR and offset)
  uint32_t table_info = platform_pci_config_read32(platform, bus, slot, func,
                                                    msix_cap + MSIX_CAP_TABLE);
  uint8_t table_bir = table_info & 0x7; // BAR Index Register (bits 2:0)
  uint32_t table_offset = table_info & ~0x7; // Table offset in BAR

  // Get BAR base address
  uint64_t bar_addr = platform_pci_read_bar(platform, bus, slot, func, table_bir);
  if (bar_addr == 0) {
    return; // BAR not configured
  }

  // Calculate MSI-X table entry address
  volatile msix_table_entry_t *msix_table =
      (volatile msix_table_entry_t *)(uintptr_t)(bar_addr + table_offset);
  volatile msix_table_entry_t *entry = &msix_table[vector_idx];

  // MSI-X message format for x86/x32:
  // Address: 0xFEE00000 | (destination_id << 12)
  // Data: delivery_mode | trigger_mode | vector
  uint32_t msg_addr_low = 0xFEE00000 | ((uint32_t)apic_id << 12);
  uint32_t msg_addr_high = 0;
  uint32_t msg_data = cpu_vector; // Fixed delivery mode, edge-triggered

  // Write MSI-X table entry
  entry->msg_addr_low = msg_addr_low;
  entry->msg_addr_high = msg_addr_high;
  entry->msg_data = msg_data;
  entry->vector_control = 0; // Unmask vector
}

// Enable MSI-X for a device
void pci_enable_msix(platform_t *platform, uint8_t bus, uint8_t slot,
                     uint8_t func) {
  // Find MSI-X capability
  uint8_t msix_cap = pci_find_msix_capability(platform, bus, slot, func);
  if (msix_cap == 0) {
    return; // MSI-X not supported
  }

  // Read current control register
  uint16_t control = platform_pci_config_read16(platform, bus, slot, func,
                                                msix_cap + MSIX_CAP_CONTROL);

  // Enable MSI-X and clear function mask
  control |= MSIX_CONTROL_ENABLE;
  control &= ~MSIX_CONTROL_FUNCTION_MASK;

  // Write back control register
  platform_pci_config_write16(platform, bus, slot, func,
                              msix_cap + MSIX_CAP_CONTROL, control);
}

// Disable legacy INTx interrupts (required when using MSI-X)
void pci_disable_intx(platform_t *platform, uint8_t bus, uint8_t slot,
                      uint8_t func) {
  uint16_t command =
      platform_pci_config_read16(platform, bus, slot, func, PCI_REG_COMMAND);
  command |= PCI_CMD_INT_DISABLE;
  platform_pci_config_write16(platform, bus, slot, func, PCI_REG_COMMAND,
                              command);
}

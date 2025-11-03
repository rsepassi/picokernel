// x86 PCI Configuration Space Access Implementation
// Uses I/O ports 0xCF8 (address) and 0xCFC (data)

#include "pci.h"
#include "io.h"
#include "platform.h"
#include "platform_impl.h"
#include "printk.h"

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
  (void)platform; // Unused on x64 (uses I/O ports)
  uint32_t address = pci_make_address(bus, slot, func, offset);
  outl(PCI_CONFIG_ADDR, address);
  return inb(PCI_CONFIG_DATA + (offset & 3));
}

// Read 16-bit value from PCI config space
uint16_t platform_pci_config_read16(platform_t *platform, uint8_t bus,
                                    uint8_t slot, uint8_t func,
                                    uint8_t offset) {
  (void)platform; // Unused on x64 (uses I/O ports)
  uint32_t address = pci_make_address(bus, slot, func, offset);
  outl(PCI_CONFIG_ADDR, address);
  return inw(PCI_CONFIG_DATA + (offset & 2));
}

// Read 32-bit value from PCI config space
uint32_t platform_pci_config_read32(platform_t *platform, uint8_t bus,
                                    uint8_t slot, uint8_t func,
                                    uint8_t offset) {
  (void)platform; // Unused on x64 (uses I/O ports)
  uint32_t address = pci_make_address(bus, slot, func, offset);
  outl(PCI_CONFIG_ADDR, address);
  return inl(PCI_CONFIG_DATA);
}

// Write 8-bit value to PCI config space
void platform_pci_config_write8(platform_t *platform, uint8_t bus, uint8_t slot,
                                uint8_t func, uint8_t offset, uint8_t value) {
  (void)platform; // Unused on x64 (uses I/O ports)
  uint32_t address = pci_make_address(bus, slot, func, offset);
  outl(PCI_CONFIG_ADDR, address);
  outb(PCI_CONFIG_DATA + (offset & 3), value);
}

// Write 16-bit value to PCI config space
void platform_pci_config_write16(platform_t *platform, uint8_t bus,
                                 uint8_t slot, uint8_t func, uint8_t offset,
                                 uint16_t value) {
  (void)platform; // Unused on x64 (uses I/O ports)
  uint32_t address = pci_make_address(bus, slot, func, offset);
  outl(PCI_CONFIG_ADDR, address);
  outw(PCI_CONFIG_DATA + (offset & 2), value);
}

// Write 32-bit value to PCI config space
void platform_pci_config_write32(platform_t *platform, uint8_t bus,
                                 uint8_t slot, uint8_t func, uint8_t offset,
                                 uint32_t value) {
  (void)platform; // Unused on x64 (uses I/O ports)
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

  // Check if this BAR is the upper half of a 64-bit BAR
  // If bar_num is odd (1, 3, 5), check the previous BAR
  if (bar_num > 0 && (bar_num & 1)) {
    uint8_t prev_bar_offset = PCI_REG_BAR0 + ((bar_num - 1) * 4);
    uint32_t prev_bar_low =
        platform_pci_config_read32(platform, bus, slot, func, prev_bar_offset);

    // Check if previous BAR is 64-bit (bits [2:1] == 0b10 means 64-bit)
    if ((prev_bar_low & 0x6) == 0x4) {
      // This BAR is the upper 32 bits of the previous 64-bit BAR
      // Return 0 to indicate it's not an independent BAR
      return 0;
    }
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
  uint8_t cap_ptr = platform_pci_config_read8(platform, bus, slot, func,
                                              PCI_REG_CAPABILITIES);
  cap_ptr &= 0xFC; // Align to 4-byte boundary

  while (cap_ptr != 0) {
    uint8_t cap_id =
        platform_pci_config_read8(platform, bus, slot, func, cap_ptr);
    if (cap_id == PCI_CAP_ID_MSIX) {
      return cap_ptr;
    }

    // Next capability
    cap_ptr = platform_pci_config_read8(platform, bus, slot, func, cap_ptr + 1);
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
  uint8_t table_bir = table_info & 0x7;      // BAR Index Register (bits 2:0)
  uint32_t table_offset = table_info & ~0x7; // Table offset in BAR

  printk("[MSI-X] MSI-X table in BAR");
  printk_dec(table_bir);
  printk(" at offset 0x");
  printk_hex32(table_offset);
  printk("\n");

  // Get BAR base address
  uint64_t bar_addr =
      platform_pci_read_bar(platform, bus, slot, func, table_bir);
  if (bar_addr == 0) {
    printk("[MSI-X] ERROR: BAR");
    printk_dec(table_bir);
    printk(" not configured for ");
    printk_dec(bus);
    printk(":");
    printk_dec(slot);
    printk(".");
    printk_dec(func);
    printk("\n");
    return; // BAR not configured
  }

  // Probe BAR size by reading BAR value after writing 0xFFFFFFFF
  uint8_t bar_reg = PCI_REG_BAR0 + (table_bir * 4);
  uint32_t orig_bar_low = platform_pci_config_read32(platform, bus, slot, func, bar_reg);
  platform_pci_config_write32(platform, bus, slot, func, bar_reg, 0xFFFFFFFF);
  uint32_t size_mask = platform_pci_config_read32(platform, bus, slot, func, bar_reg);
  platform_pci_config_write32(platform, bus, slot, func, bar_reg, orig_bar_low);

  size_mask &= ~0xF; // Clear flags
  uint32_t bar_size = (~size_mask) + 1;

  printk("[MSI-X] BAR");
  printk_dec(table_bir);
  printk(" size: ");
  printk_dec(bar_size);
  printk(" bytes (0x");
  printk_hex32(bar_size);
  printk(")\n");

  // Read MSI-X table size from Message Control register
  uint16_t msg_control = platform_pci_config_read16(platform, bus, slot, func,
                                                     msix_cap + MSIX_CAP_CONTROL);
  uint16_t table_size = (msg_control & 0x7FF) + 1; // Bits 0-10 = table size minus 1

  printk("[MSI-X] Table: size=");
  printk_dec(table_size);
  printk(" entries\n");

  // Calculate MSI-X table entry address
  uint64_t entry_addr = bar_addr + table_offset + (vector_idx * sizeof(msix_table_entry_t));
  volatile msix_table_entry_t *msix_table =
      (volatile msix_table_entry_t *)(bar_addr + table_offset);
  volatile msix_table_entry_t *entry = &msix_table[vector_idx];

  printk("[MSI-X] Entry address: 0x");
  printk_hex64(entry_addr);
  printk(" (vector ");
  printk_dec(vector_idx);
  printk("/");
  printk_dec(table_size);
  printk(")\n");

  // MSI-X message format for x86/x64:
  // Address: Always 0xFEE00000 (hardcoded MSI address) | (destination_id << 12)
  // Data: [15:trigger(0=edge)][14:level][10-8:delivery(000=fixed)][7-0:vector]
  // Note: x86 MSI/MSI-X always use 0xFEE00000 base, not actual LAPIC base
  uint32_t msg_addr_low = 0xFEE00000 | ((uint32_t)apic_id << 12);
  uint32_t msg_addr_high = 0;
  // Fixed delivery mode, edge-triggered (bit 15=0, bit 14=0)
  uint32_t msg_data = cpu_vector;

  printk("[MSI-X] ");
  printk_dec(bus);
  printk(":");
  printk_dec(slot);
  printk(".");
  printk_dec(func);
  printk(" vec[");
  printk_dec(vector_idx);
  printk("]: BAR");
  printk_dec(table_bir);
  printk("=0x");
  printk_hex64(bar_addr);
  printk(" off=0x");
  printk_hex32(table_offset);
  printk(" -> vec");
  printk_dec(cpu_vector);
  printk(" addr=0x");
  printk_hex32(msg_addr_low);
  printk("\n");

  printk("[MSI-X] Writing: addr_lo=0x");
  printk_hex32(msg_addr_low);
  printk(" addr_hi=0x");
  printk_hex32(msg_addr_high);
  printk(" data=0x");
  printk_hex32(msg_data);
  printk(" ctrl=0\n");

  // Write MSI-X table entry (with memory barriers to ensure ordering)
  entry->msg_addr_low = msg_addr_low;
  __asm__ volatile("mfence" ::: "memory");
  entry->msg_addr_high = msg_addr_high;
  __asm__ volatile("mfence" ::: "memory");
  entry->msg_data = msg_data;
  __asm__ volatile("mfence" ::: "memory");
  entry->vector_control = 0; // Unmask vector
  __asm__ volatile("mfence" ::: "memory");

  // Read back MSI-X entry to verify writes
  uint32_t rb_addr_low = entry->msg_addr_low;
  uint32_t rb_addr_high = entry->msg_addr_high;
  uint32_t rb_data = entry->msg_data;
  uint32_t rb_ctrl = entry->vector_control;

  printk("[MSI-X] Readback: addr_lo=0x");
  printk_hex32(rb_addr_low);
  printk(" addr_hi=0x");
  printk_hex32(rb_addr_high);
  printk(" data=0x");
  printk_hex32(rb_data);
  printk(" ctrl=0x");
  printk_hex32(rb_ctrl);
  printk("\n");
}

// Enable MSI-X for a device
void pci_enable_msix(platform_t *platform, uint8_t bus, uint8_t slot,
                     uint8_t func) {
  // Find MSI-X capability
  uint8_t msix_cap = pci_find_msix_capability(platform, bus, slot, func);
  if (msix_cap == 0) {
    return; // MSI-X not supported
  }

  // Verify PCI Command register
  uint16_t command =
      platform_pci_config_read16(platform, bus, slot, func, PCI_REG_COMMAND);
  printk("[MSI-X] PCI Command: 0x");
  printk_hex16(command);
  printk(" (bit 10 INT_DISABLE should be ");
  printk((command & (1 << 10)) ? "1" : "0");
  printk(")\n");

  // Read current control register
  uint16_t control = platform_pci_config_read16(platform, bus, slot, func,
                                                msix_cap + MSIX_CAP_CONTROL);

  // Enable MSI-X and clear function mask
  control |= MSIX_CONTROL_ENABLE;
  control &= ~MSIX_CONTROL_FUNCTION_MASK;

  // Write back control register
  platform_pci_config_write16(platform, bus, slot, func,
                              msix_cap + MSIX_CAP_CONTROL, control);

  // Verify MSI-X is enabled
  uint16_t verify = platform_pci_config_read16(platform, bus, slot, func,
                                               msix_cap + MSIX_CAP_CONTROL);
  printk("[MSI-X] ");
  printk_dec(bus);
  printk(":");
  printk_dec(slot);
  printk(".");
  printk_dec(func);
  printk(" enabled: ctrl=0x");
  printk_hex16(verify);
  printk(" (bit 15 Enable=");
  printk((verify & (1 << 15)) ? "1" : "0");
  printk(", bit 14 FnMask=");
  printk((verify & (1 << 14)) ? "1" : "0");
  printk(")\n");
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

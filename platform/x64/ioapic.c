// x32 I/O APIC Implementation
// Modern interrupt routing for PCI and ISA devices

#include "ioapic.h"
#include "acpi.h"
#include "platform.h"
#include "platform_impl.h"
#include "printk.h"

// IOAPIC indirect register access (memory-mapped)
#define IOREGSEL_OFFSET 0x00 // Register select
#define IOWIN_OFFSET 0x10    // Register window (data)

// Helper: Memory-mapped register access
static inline void mmio_write32(volatile uint32_t *addr, uint32_t value) {
  *addr = value;
  __asm__ volatile("mfence" ::: "memory");
}

static inline uint32_t mmio_read32(volatile uint32_t *addr) {
  __asm__ volatile("mfence" ::: "memory");
  return *addr;
}

// IOAPIC register read (indirect addressing)
static uint32_t ioapic_read_reg(ioapic_t *ioapic, uint8_t reg) {
  volatile uint32_t *ioregsel =
      (volatile uint32_t *)(uintptr_t)ioapic->base_addr;
  volatile uint32_t *iowin =
      (volatile uint32_t *)(uintptr_t)(ioapic->base_addr + IOWIN_OFFSET);

  mmio_write32(ioregsel, reg);
  return mmio_read32(iowin);
}

// IOAPIC register write (indirect addressing)
static void ioapic_write_reg(ioapic_t *ioapic, uint8_t reg, uint32_t value) {
  volatile uint32_t *ioregsel =
      (volatile uint32_t *)(uintptr_t)ioapic->base_addr;
  volatile uint32_t *iowin =
      (volatile uint32_t *)(uintptr_t)(ioapic->base_addr + IOWIN_OFFSET);

  mmio_write32(ioregsel, reg);
  mmio_write32(iowin, value);
}

// Read 64-bit redirection entry (two 32-bit registers)
static uint64_t ioapic_read_redtbl(ioapic_t *ioapic, uint8_t entry) {
  uint32_t low = ioapic_read_reg(ioapic, IOAPIC_REDTBL_BASE + entry * 2);
  uint32_t high = ioapic_read_reg(ioapic, IOAPIC_REDTBL_BASE + entry * 2 + 1);
  return ((uint64_t)high << 32) | low;
}

// Write 64-bit redirection entry
static void ioapic_write_redtbl(ioapic_t *ioapic, uint8_t entry,
                                uint64_t value) {
  uint32_t low = value & 0xFFFFFFFF;
  uint32_t high = (value >> 32) & 0xFFFFFFFF;

  ioapic_write_reg(ioapic, IOAPIC_REDTBL_BASE + entry * 2, low);
  ioapic_write_reg(ioapic, IOAPIC_REDTBL_BASE + entry * 2 + 1, high);
}

// Find all IOAPICs in ACPI MADT (can be 1 or 2)
static int find_ioapics_in_madt(platform_t *platform) {
  struct acpi_table_header *madt_header =
      acpi_find_table(platform, ACPI_SIG_MADT);
  if (madt_header == NULL) {
    return -1;
  }

  struct acpi_madt *madt = (struct acpi_madt *)madt_header;

  // Walk MADT entries
  uint8_t *ptr = (uint8_t *)madt + sizeof(struct acpi_madt);
  uint8_t *end = (uint8_t *)madt + madt->header.length;
  platform->num_ioapics = 0;

  while (ptr < end && platform->num_ioapics < 2) {
    struct acpi_madt_entry_header *entry = (struct acpi_madt_entry_header *)ptr;

    if (entry->type == ACPI_MADT_TYPE_IO_APIC) {
      struct acpi_madt_io_apic *ioapic_entry =
          (struct acpi_madt_io_apic *)entry;

      uint8_t idx = platform->num_ioapics;
      platform->ioapic[idx].base_addr = ioapic_entry->io_apic_address;
      platform->ioapic[idx].gsi_base = ioapic_entry->global_irq_base;
      platform->ioapic[idx].id = ioapic_entry->io_apic_id;
      platform->num_ioapics++;
    }

    ptr += entry->length;
  }

  return platform->num_ioapics > 0 ? 0 : -1;
}

// Initialize all IOAPICs
void ioapic_init(platform_t *platform) {
  // Find all IOAPICs in ACPI MADT
  if (find_ioapics_in_madt(platform) < 0) {
    // ACPI not available - use standard x86 IOAPIC address
    printk("ACPI MADT not available, using standard IOAPIC address 0xFEC00000\n");
    platform->num_ioapics = 1;
    platform->ioapic[0].base_addr = 0xFEC00000;
    platform->ioapic[0].gsi_base = 0;
    platform->ioapic[0].id = 0;
  }

  // Initialize each IOAPIC
  for (uint8_t ioapic_idx = 0; ioapic_idx < platform->num_ioapics; ioapic_idx++) {
    ioapic_t *ioapic = &platform->ioapic[ioapic_idx];

    // Read version and max entries to verify IOAPIC is present
    uint32_t version = ioapic_read_reg(ioapic, IOAPIC_REG_VERSION);
    ioapic->max_entries = ((version >> 16) & 0xFF) + 1;

    // Sanity check: max_entries should be reasonable (typically 24)
    if (ioapic->max_entries == 0 || ioapic->max_entries > 240) {
      printk("ERROR: IOAPIC #");
      printk_dec(ioapic_idx);
      printk(" not found or invalid (max_entries=");
      printk_dec(ioapic->max_entries);
      printk(")\n");
      ioapic->base_addr = 0;
      ioapic->max_entries = 0;
      continue;
    }

    // Mask all interrupts initially
    for (uint8_t i = 0; i < ioapic->max_entries; i++) {
      uint64_t entry = IOAPIC_MASK;
      ioapic_write_redtbl(ioapic, i, entry);
    }

    printk("IOAPIC #");
    printk_dec(ioapic_idx);
    printk(" initialized at 0x");
    printk_hex32(ioapic->base_addr);
    printk(" (GSI base ");
    printk_dec(ioapic->gsi_base);
    printk(", ");
    printk_dec(ioapic->max_entries);
    printk(" entries)\n");
  }
}

// Route a GSI to a vector with specified trigger mode and polarity
// gsi: Global System Interrupt number (maps to IOAPIC + pin)
// trigger: 0 = edge-triggered, 1 = level-triggered (PCI INTx, MMIO)
// polarity: 0 = active-high, 1 = active-low
void ioapic_route_irq(platform_t *platform, uint8_t gsi, uint8_t vector,
                      uint8_t apic_id, uint8_t trigger, uint8_t polarity) {
  // Find which IOAPIC handles this GSI
  ioapic_t *ioapic = NULL;
  uint8_t pin = 0;
  uint8_t ioapic_idx = 0;

  for (uint8_t i = 0; i < platform->num_ioapics; i++) {
    ioapic_t *candidate = &platform->ioapic[i];
    if (gsi >= candidate->gsi_base &&
        gsi < candidate->gsi_base + candidate->max_entries) {
      ioapic = candidate;
      pin = gsi - candidate->gsi_base;
      ioapic_idx = i;
      break;
    }
  }

  if (ioapic == NULL || ioapic->base_addr == 0) {
    printk("[IOAPIC] ERROR: No IOAPIC found for GSI ");
    printk_dec(gsi);
    printk("\n");
    return;
  }

  // Select trigger mode and polarity based on parameters
  uint64_t trigger_mode = trigger ? IOAPIC_TRIGGER_LEVEL : IOAPIC_TRIGGER_EDGE;
  uint64_t polarity_mode = polarity ? IOAPIC_INTPOL_LOW : IOAPIC_INTPOL_HIGH;

  // Build redirection entry
  uint64_t entry = (uint64_t)vector | IOAPIC_DELMOD_FIXED |
                   IOAPIC_DEST_PHYSICAL | polarity_mode |
                   trigger_mode | IOAPIC_MASK |
                   ((uint64_t)apic_id << 56);

  ioapic_write_redtbl(ioapic, pin, entry);

  // Debug: Read back and verify
  printk("[IOAPIC #");
  printk_dec(ioapic_idx);
  printk("] GSI ");
  printk_dec(gsi);
  printk(" (pin ");
  printk_dec(pin);
  printk(") -> vec=");
  printk_dec(vector);
  printk(" apic=");
  printk_dec(apic_id);
  printk(" trig=");
  printk_dec(trigger);
  printk(" pol=");
  printk_dec(polarity);
  printk("\n");
}

// Mask a GSI
void ioapic_mask_irq(platform_t *platform, uint8_t gsi) {
  // Find which IOAPIC handles this GSI
  for (uint8_t i = 0; i < platform->num_ioapics; i++) {
    ioapic_t *ioapic = &platform->ioapic[i];
    if (gsi >= ioapic->gsi_base &&
        gsi < ioapic->gsi_base + ioapic->max_entries) {
      uint8_t pin = gsi - ioapic->gsi_base;
      uint64_t entry = ioapic_read_redtbl(ioapic, pin);
      entry |= IOAPIC_MASK;
      ioapic_write_redtbl(ioapic, pin, entry);
      return;
    }
  }
}

// Unmask a GSI
void ioapic_unmask_irq(platform_t *platform, uint8_t gsi) {
  // Find which IOAPIC handles this GSI
  for (uint8_t i = 0; i < platform->num_ioapics; i++) {
    ioapic_t *ioapic = &platform->ioapic[i];
    if (gsi >= ioapic->gsi_base &&
        gsi < ioapic->gsi_base + ioapic->max_entries) {
      uint8_t pin = gsi - ioapic->gsi_base;
      uint64_t entry = ioapic_read_redtbl(ioapic, pin);
      entry &= ~IOAPIC_MASK;
      ioapic_write_redtbl(ioapic, pin, entry);

      // Debug: Read back and verify unmask
      uint64_t readback = ioapic_read_redtbl(ioapic, pin);
      printk("[IOAPIC #");
      printk_dec(i);
      printk("] Unmasked GSI ");
      printk_dec(gsi);
      printk(" (pin ");
      printk_dec(pin);
      printk(") entry=0x");
      printk_hex64(readback);
      printk("\n");
      return;
    }
  }
}

// Get IOAPIC info for debugging (returns first IOAPIC)
const ioapic_t *ioapic_get_info(platform_t *platform) {
  return &platform->ioapic[0];
}

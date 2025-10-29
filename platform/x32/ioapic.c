// x32 I/O APIC Implementation
// Modern interrupt routing for PCI and ISA devices

#include "ioapic.h"
#include "acpi.h"
#include "printk.h"

// Default IOAPIC address (can be overridden by ACPI MADT)
#define IOAPIC_DEFAULT_BASE 0xFEC00000

// IOAPIC indirect register access (memory-mapped)
#define IOREGSEL_OFFSET 0x00 // Register select
#define IOWIN_OFFSET 0x10    // Register window (data)

// Global IOAPIC state
static ioapic_t g_ioapic = {0};

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
static uint32_t ioapic_read_reg(uint8_t reg) {
  volatile uint32_t *ioregsel =
      (volatile uint32_t *)(uintptr_t)g_ioapic.base_addr;
  volatile uint32_t *iowin =
      (volatile uint32_t *)(uintptr_t)(g_ioapic.base_addr + IOWIN_OFFSET);

  mmio_write32(ioregsel, reg);
  return mmio_read32(iowin);
}

// IOAPIC register write (indirect addressing)
static void ioapic_write_reg(uint8_t reg, uint32_t value) {
  volatile uint32_t *ioregsel =
      (volatile uint32_t *)(uintptr_t)g_ioapic.base_addr;
  volatile uint32_t *iowin =
      (volatile uint32_t *)(uintptr_t)(g_ioapic.base_addr + IOWIN_OFFSET);

  mmio_write32(ioregsel, reg);
  mmio_write32(iowin, value);
}

// Read 64-bit redirection entry (two 32-bit registers)
static uint64_t ioapic_read_redtbl(uint8_t entry) {
  uint32_t low = ioapic_read_reg(IOAPIC_REDTBL_BASE + entry * 2);
  uint32_t high = ioapic_read_reg(IOAPIC_REDTBL_BASE + entry * 2 + 1);
  return ((uint64_t)high << 32) | low;
}

// Write 64-bit redirection entry
static void ioapic_write_redtbl(uint8_t entry, uint64_t value) {
  uint32_t low = value & 0xFFFFFFFF;
  uint32_t high = (value >> 32) & 0xFFFFFFFF;

  ioapic_write_reg(IOAPIC_REDTBL_BASE + entry * 2, low);
  ioapic_write_reg(IOAPIC_REDTBL_BASE + entry * 2 + 1, high);
}

// Find IOAPIC in ACPI MADT
static int find_ioapic_in_madt(void) {
  struct acpi_table_header *madt_header = acpi_find_table(ACPI_SIG_MADT);
  if (madt_header == NULL) {
    return -1;
  }

  struct acpi_madt *madt = (struct acpi_madt *)madt_header;

  // Walk MADT entries
  uint8_t *ptr = (uint8_t *)madt + sizeof(struct acpi_madt);
  uint8_t *end = (uint8_t *)madt + madt->header.length;

  while (ptr < end) {
    struct acpi_madt_entry_header *entry = (struct acpi_madt_entry_header *)ptr;

    if (entry->type == ACPI_MADT_TYPE_IO_APIC) {
      struct acpi_madt_io_apic *ioapic_entry =
          (struct acpi_madt_io_apic *)entry;

      g_ioapic.base_addr = ioapic_entry->io_apic_address;
      g_ioapic.gsi_base = ioapic_entry->global_irq_base;
      g_ioapic.id = ioapic_entry->io_apic_id;

      return 0;
    }

    ptr += entry->length;
  }

  return -1;
}

// Initialize IOAPIC
void ioapic_init(void) {
  // Find IOAPIC in ACPI MADT
  if (find_ioapic_in_madt() < 0) {
    g_ioapic.base_addr = IOAPIC_DEFAULT_BASE;
    g_ioapic.gsi_base = 0;
    g_ioapic.id = 0;
  }

  // Read version and max entries
  uint32_t version = ioapic_read_reg(IOAPIC_REG_VERSION);
  g_ioapic.max_entries = ((version >> 16) & 0xFF) + 1;

  // Mask all interrupts initially
  for (uint8_t i = 0; i < g_ioapic.max_entries; i++) {
    uint64_t entry = IOAPIC_MASK;
    ioapic_write_redtbl(i, entry);
  }

  printk("IOAPIC initialized\n");
}

// Route an IRQ to a vector
void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t apic_id) {
  if (irq >= g_ioapic.max_entries) {
    return;
  }

  // Build redirection entry for level-triggered PCI interrupt
  uint64_t entry = (uint64_t)vector | IOAPIC_DELMOD_FIXED |
                   IOAPIC_DEST_PHYSICAL | IOAPIC_INTPOL_HIGH |
                   IOAPIC_TRIGGER_LEVEL | IOAPIC_MASK |
                   ((uint64_t)apic_id << 56);

  ioapic_write_redtbl(irq, entry);
}

// Mask an IRQ
void ioapic_mask_irq(uint8_t irq) {
  if (irq >= g_ioapic.max_entries) {
    return;
  }

  uint64_t entry = ioapic_read_redtbl(irq);
  entry |= IOAPIC_MASK;
  ioapic_write_redtbl(irq, entry);
}

// Unmask an IRQ
void ioapic_unmask_irq(uint8_t irq) {
  if (irq >= g_ioapic.max_entries) {
    return;
  }

  uint64_t entry = ioapic_read_redtbl(irq);
  entry &= ~IOAPIC_MASK;
  ioapic_write_redtbl(irq, entry);
}

// Get IOAPIC info for debugging
const ioapic_t *ioapic_get_info(void) { return &g_ioapic; }

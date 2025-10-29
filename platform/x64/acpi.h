// ACPI (Advanced Configuration and Power Interface) structures
// Defines data structures for parsing ACPI tables on x86_64

#pragma once

#include <stdint.h>

// ACPI table signatures (4-character codes)
#define ACPI_SIG_RSDP                                                          \
  "RSD PTR " // Root System Description Pointer (note: 8 chars with space)
#define ACPI_SIG_RSDT "RSDT" // Root System Description Table
#define ACPI_SIG_XSDT "XSDT" // Extended System Description Table
#define ACPI_SIG_MADT "APIC" // Multiple APIC Description Table
#define ACPI_SIG_FADT "FACP" // Fixed ACPI Description Table

// RSDP (Root System Description Pointer)
// This is the first structure we find by scanning BIOS memory
struct acpi_rsdp {
  char signature[8];     // "RSD PTR "
  uint8_t checksum;      // Checksum of first 20 bytes
  char oem_id[6];        // OEM ID
  uint8_t revision;      // ACPI revision (0=1.0, 2=2.0+)
  uint32_t rsdt_address; // Physical address of RSDT

  // Fields below only exist if revision >= 2
  uint32_t length;           // Length of entire table
  uint64_t xsdt_address;     // Physical address of XSDT (64-bit)
  uint8_t extended_checksum; // Checksum of entire table
  uint8_t reserved[3];
} __attribute__((packed));

// Generic ACPI table header
// All ACPI tables (except RSDP) start with this header
struct acpi_table_header {
  char signature[4];         // Table signature (e.g., "APIC", "FACP")
  uint32_t length;           // Length of entire table including header
  uint8_t revision;          // Table revision
  uint8_t checksum;          // Checksum of entire table
  char oem_id[6];            // OEM ID
  char oem_table_id[8];      // OEM table ID
  uint32_t oem_revision;     // OEM revision
  uint32_t creator_id;       // Creator/compiler ID
  uint32_t creator_revision; // Creator/compiler revision
} __attribute__((packed));

// RSDT (Root System Description Table)
// Contains pointers to other ACPI tables (32-bit addresses)
struct acpi_rsdt {
  struct acpi_table_header header;
  uint32_t entry[1]; // Array of physical addresses (variable length)
} __attribute__((packed));

// XSDT (Extended System Description Table)
// Contains pointers to other ACPI tables (64-bit addresses)
struct acpi_xsdt {
  struct acpi_table_header header;
  uint64_t entry[1]; // Array of physical addresses (variable length)
} __attribute__((packed));

// MADT (Multiple APIC Description Table)
// Describes interrupt controllers and CPUs
struct acpi_madt {
  struct acpi_table_header header;
  uint32_t local_apic_address; // Physical address of local APIC
  uint32_t flags;              // Flags (bit 0 = PCAT_COMPAT)
  // Followed by variable-length interrupt controller structures
} __attribute__((packed));

// MADT Entry Types
#define ACPI_MADT_TYPE_LOCAL_APIC 0
#define ACPI_MADT_TYPE_IO_APIC 1
#define ACPI_MADT_TYPE_INTERRUPT_OVERRIDE 2
#define ACPI_MADT_TYPE_NMI_SOURCE 3
#define ACPI_MADT_TYPE_LOCAL_APIC_NMI 4
#define ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE 5
#define ACPI_MADT_TYPE_LOCAL_X2APIC 9

// Generic MADT entry header
struct acpi_madt_entry_header {
  uint8_t type;
  uint8_t length;
} __attribute__((packed));

// MADT Local APIC structure (CPU entry)
struct acpi_madt_local_apic {
  struct acpi_madt_entry_header header;
  uint8_t acpi_processor_id;
  uint8_t apic_id;
  uint32_t flags; // Bit 0: Processor enabled, Bit 1: Online capable
} __attribute__((packed));

// MADT I/O APIC structure (Interrupt controller entry)
struct acpi_madt_io_apic {
  struct acpi_madt_entry_header header;
  uint8_t io_apic_id;
  uint8_t reserved;
  uint32_t io_apic_address;
  uint32_t global_irq_base;
} __attribute__((packed));

// MADT Interrupt Source Override
struct acpi_madt_interrupt_override {
  struct acpi_madt_entry_header header;
  uint8_t bus;         // Always 0 (ISA)
  uint8_t source;      // IRQ source
  uint32_t global_irq; // Global system interrupt
  uint16_t flags;      // Polarity and trigger mode
} __attribute__((packed));

// Forward declaration and typedef for platform
struct platform_t;
typedef struct platform_t platform_t;

// Function declarations
void *acpi_find_rsdp(void);
struct acpi_table_header *acpi_find_table(platform_t *platform, const char *signature);
void acpi_init(platform_t *platform);
void acpi_dump_tables(platform_t *platform);

// fw_cfg accessors
uint64_t fw_cfg_read_ram_size(void);
uint32_t fw_cfg_read_nb_cpus(void);

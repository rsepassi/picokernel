// ACPI (Advanced Configuration and Power Interface) implementation
// Provides device discovery by parsing ACPI tables

#include "acpi.h"
#include "io.h"
#include "platform_impl.h"
#include "printk.h"

// QEMU fw_cfg interface (IOport-based for x86)
#define FW_CFG_PORT_SEL 0x510
#define FW_CFG_PORT_DATA 0x511
#define FW_CFG_SIGNATURE 0x0000 // Signature selector (returns "QEMU")
#define FW_CFG_ID 0x0001        // ID selector
#define FW_CFG_RAM_SIZE 0x0003  // RAM size in bytes
#define FW_CFG_NB_CPUS 0x0005   // Number of CPUs
#define FW_CFG_FILE_DIR 0x0019  // File directory selector

// fw_cfg file entry structure
struct fw_cfg_file {
  uint32_t size;   // big-endian
  uint16_t select; // big-endian
  uint16_t reserved;
  char name[56];
} __attribute__((packed));

// Memory comparison function
static int memcmp(const void *s1, const void *s2, unsigned long n) {
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;

  for (unsigned long i = 0; i < n; i++) {
    if (p1[i] != p2[i]) {
      return p1[i] - p2[i];
    }
  }
  return 0;
}

// Calculate checksum for ACPI tables
static uint8_t acpi_checksum(void *addr, uint32_t length) {
  uint8_t sum = 0;
  uint8_t *ptr = (uint8_t *)addr;

  // Use volatile to prevent compiler optimizations that might cause issues
  for (volatile uint32_t i = 0; i < length; i++) {
    sum += ptr[i];
  }

  return sum;
}


// Byte swap helpers for big-endian fw_cfg data
static uint32_t bswap32(uint32_t x) {
  return ((x & 0xFF000000) >> 24) | ((x & 0x00FF0000) >> 8) |
         ((x & 0x0000FF00) << 8) | ((x & 0x000000FF) << 24);
}

static uint16_t bswap16(uint16_t x) {
  return ((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8);
}

// String comparison
static int strcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

// Read data from fw_cfg
static void fw_cfg_read_data(uint8_t *buf, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    buf[i] = inb(FW_CFG_PORT_DATA);
  }
}

// Find RSDP using QEMU fw_cfg interface
static void *fw_cfg_find_rsdp(void) {
  // Select file directory
  outw(FW_CFG_PORT_SEL, FW_CFG_FILE_DIR);

  // Read file count (big-endian)
  uint32_t count;
  fw_cfg_read_data((uint8_t *)&count, sizeof(count));
  count = bswap32(count);

  if (count == 0 || count > 100) {
    return NULL;
  }

  // Buffers for RSDP and ACPI tables
  static uint8_t rsdp_buf[64];      // RSDP is 36 bytes max
  static uint8_t tables_buf[16384]; // 16KB for ACPI tables
  struct acpi_rsdp *rsdp = NULL;
  uint32_t tables_size = 0;

  // Remember selectors for files we want to load
  uint16_t rsdp_selector = 0;
  uint32_t rsdp_size = 0;
  uint16_t tables_selector = 0;
  uint32_t tables_file_size = 0;

  // Single pass: find the files we need
  for (uint32_t i = 0; i < count; i++) {
    struct fw_cfg_file file;
    fw_cfg_read_data((uint8_t *)&file, sizeof(file));

    // Convert big-endian fields
    file.size = bswap32(file.size);
    file.select = bswap16(file.select);

    // Check if this is the RSDP file
    if (strcmp(file.name, "etc/acpi/rsdp") == 0) {
      rsdp_selector = file.select;
      rsdp_size = file.size;
    }
    // Check if this is the tables file
    else if (strcmp(file.name, "etc/acpi/tables") == 0) {
      tables_selector = file.select;
      tables_file_size = file.size;
    }
  }

  // Load the files
  if (rsdp_selector != 0 && rsdp_size <= sizeof(rsdp_buf)) {
    outw(FW_CFG_PORT_SEL, rsdp_selector);
    fw_cfg_read_data(rsdp_buf, rsdp_size);

    // Verify signature
    rsdp = (struct acpi_rsdp *)rsdp_buf;
    if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0) {
      return NULL;
    }
  }

  if (tables_selector != 0 && tables_file_size <= sizeof(tables_buf)) {
    outw(FW_CFG_PORT_SEL, tables_selector);
    fw_cfg_read_data(tables_buf, tables_file_size);
    tables_size = tables_file_size;
    // The etc/acpi/tables file contains the individual ACPI tables,
    // but NOT the RSDT itself. We need to build the RSDT dynamically.
    // Scan through all tables and collect their addresses.

    uint8_t *ptr = tables_buf;
    uint8_t *end = tables_buf + tables_file_size;
    uint32_t table_addresses[32]; // Max 32 tables
    uint32_t table_count = 0;

    while (ptr + sizeof(struct acpi_table_header) < end && table_count < 32) {
      struct acpi_table_header *header = (struct acpi_table_header *)ptr;

      // Check if we've gone past valid data
      if (header->length == 0 ||
          header->length < sizeof(struct acpi_table_header) ||
          header->length > (uint32_t)(end - ptr)) {
        break;
      }

      // Skip FACS and RSDT (we'll build our own)
      // We only need MADT for interrupt controller discovery
      if (memcmp(header->signature, "APIC", 4) == 0) {
        table_addresses[table_count++] = (uint32_t)(uintptr_t)ptr;
      }

      // Move to next table
      ptr += header->length;
    }

    // Build RSDT
    static uint8_t rsdt_buffer[160]; // Header (36) + 32 entries (4 bytes each)
    struct acpi_rsdt *rsdt_build = (struct acpi_rsdt *)rsdt_buffer;

    // Set signature
    rsdt_build->header.signature[0] = 'R';
    rsdt_build->header.signature[1] = 'S';
    rsdt_build->header.signature[2] = 'D';
    rsdt_build->header.signature[3] = 'T';

    rsdt_build->header.length =
        sizeof(struct acpi_table_header) + (table_count * 4);
    rsdt_build->header.revision = 1;
    rsdt_build->header.checksum = 0;

    // Copy OEM fields from RSDP
    for (int i = 0; i < 6; i++) {
      rsdt_build->header.oem_id[i] = rsdp->oem_id[i];
    }

    // Copy table addresses
    for (uint32_t i = 0; i < table_count; i++) {
      rsdt_build->entry[i] = table_addresses[i];
    }

    // Calculate checksum
    uint8_t sum = 0;
    volatile uint32_t len = rsdt_build->header.length;
    for (volatile uint32_t i = 0; i < len; i++) {
      sum += rsdt_buffer[i];
    }
    rsdt_build->header.checksum = (uint8_t)(0 - sum);

    // Update RSDP to point to our built RSDT
    rsdp->rsdt_address = (uint32_t)(uintptr_t)rsdt_buffer;
    // Clear XSDT address since we only built RSDT
    rsdp->xsdt_address = 0;
    // Set revision to 0 to indicate ACPI 1.0 (RSDT only)
    rsdp->revision = 0;
  }

  return (rsdp && tables_size > 0) ? rsdp : NULL;
}

// Find RSDP using fw_cfg (for QEMU -kernel boot)
void *acpi_find_rsdp(void) { return fw_cfg_find_rsdp(); }

// Find a specific ACPI table by signature
struct acpi_table_header *acpi_find_table(platform_t *platform, const char *signature) {
  if (platform->rsdp == NULL) {
    return NULL;
  }

  // Use XSDT if available (ACPI 2.0+), otherwise use RSDT
  if (platform->rsdp->revision >= 2 && platform->rsdp->xsdt_address != 0) {
    // Use XSDT (64-bit pointers)
    struct acpi_xsdt *xsdt =
        (struct acpi_xsdt *)(uintptr_t)platform->rsdp->xsdt_address;

    // Verify XSDT checksum
    if (acpi_checksum(xsdt, xsdt->header.length) != 0) {
      printk("XSDT checksum failed\n");
      return NULL;
    }

    // Calculate number of entries
    uint32_t entries =
        (xsdt->header.length - sizeof(struct acpi_table_header)) / 8;

    // Search for table with matching signature
    for (uint32_t i = 0; i < entries; i++) {
      struct acpi_table_header *header =
          (struct acpi_table_header *)(uintptr_t)xsdt->entry[i];

      if (memcmp(header->signature, signature, 4) == 0) {
        // Note: Skip checksum verification for tables from fw_cfg
        return header;
      }
    }
  } else if (platform->rsdp->rsdt_address != 0) {
    // Use RSDT (32-bit pointers)
    struct acpi_rsdt *rsdt =
        (struct acpi_rsdt *)(uintptr_t)platform->rsdp->rsdt_address;

    // Verify RSDT checksum
    if (acpi_checksum(rsdt, rsdt->header.length) != 0) {
      printk("RSDT checksum failed\n");
      return NULL;
    }

    // Calculate number of entries
    volatile uint32_t entries =
        (rsdt->header.length - sizeof(struct acpi_table_header)) / 4;

    // Search for table with matching signature
    for (volatile uint32_t i = 0; i < entries; i++) {
      struct acpi_table_header *header =
          (struct acpi_table_header *)(uintptr_t)rsdt->entry[i];

      if (memcmp(header->signature, signature, 4) == 0) {
        // Note: Skip checksum verification for tables from fw_cfg
        // as QEMU provides them without proper checksums
        return header;
      }
    }
  }

  return NULL; // Table not found
}

// Initialize ACPI subsystem
void acpi_init(platform_t *platform) {
  // Find RSDP
  platform->rsdp = (struct acpi_rsdp *)acpi_find_rsdp();

  if (platform->rsdp == NULL) {
    printk("ACPI: RSDP not found\n");
    return;
  }

  printk("ACPI initialized (RSDP at ");
  printk_hex64((uint64_t)platform->rsdp);
  printk(", Revision ");
  printk_dec(platform->rsdp->revision);
  printk(", OEM: ");
  for (int i = 0; i < 6; i++) {
    char c = platform->rsdp->oem_id[i];
    printk_putc(c >= 32 && c <= 126 ? c : '?');
  }
  printk(")\n\n");
}

// Helper to dump detailed MADT info
static void dump_madt_details(struct acpi_table_header *header) {
  struct acpi_madt *madt = (struct acpi_madt *)header;

  printk("      Local APIC Address: ");
  printk_hex32(madt->local_apic_address);
  printk("\n      Flags: ");
  printk_hex32(madt->flags);
  printk("\n      Entries:\n");

  uint8_t *ptr = (uint8_t *)madt + sizeof(struct acpi_madt);
  uint8_t *end = (uint8_t *)madt + madt->header.length;

  while (ptr < end) {
    struct acpi_madt_entry_header *entry = (struct acpi_madt_entry_header *)ptr;

    printk("        Type ");
    printk_dec(entry->type);
    printk(", Length ");
    printk_dec(entry->length);

    if (entry->type == ACPI_MADT_TYPE_LOCAL_APIC) {
      struct acpi_madt_local_apic *lapic = (struct acpi_madt_local_apic *)entry;
      printk(" (Local APIC: processor_id=");
      printk_dec(lapic->acpi_processor_id);
      printk(", apic_id=");
      printk_dec(lapic->apic_id);
      printk(", flags=");
      printk_hex32(lapic->flags);
      printk(")");
    } else if (entry->type == ACPI_MADT_TYPE_IO_APIC) {
      struct acpi_madt_io_apic *ioapic = (struct acpi_madt_io_apic *)entry;
      printk(" (I/O APIC: id=");
      printk_dec(ioapic->io_apic_id);
      printk(", addr=");
      printk_hex32(ioapic->io_apic_address);
      printk(", irq_base=");
      printk_dec(ioapic->global_irq_base);
      printk(")");
    } else if (entry->type == ACPI_MADT_TYPE_INTERRUPT_OVERRIDE) {
      struct acpi_madt_interrupt_override *override =
          (struct acpi_madt_interrupt_override *)entry;
      printk(" (Interrupt Override: bus=");
      printk_dec(override->bus);
      printk(", source=");
      printk_dec(override->source);
      printk(", global_irq=");
      printk_dec(override->global_irq);
      printk(", flags=");
      printk_hex16(override->flags);
      printk(")");
    } else if (entry->type == ACPI_MADT_TYPE_LOCAL_APIC_NMI) {
      printk(" (Local APIC NMI)");
    } else if (entry->type == ACPI_MADT_TYPE_NMI_SOURCE) {
      printk(" (NMI Source)");
    } else if (entry->type == ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE) {
      printk(" (Local APIC Address Override)");
    } else if (entry->type == ACPI_MADT_TYPE_LOCAL_X2APIC) {
      printk(" (Local x2APIC)");
    }

    printk("\n");
    ptr += entry->length;
  }
}

// Dump all ACPI tables for debugging
void acpi_dump_tables(platform_t *platform) {
  if (platform->rsdp == NULL) {
    printk("ACPI not initialized\n");
    return;
  }

  printk("=== ACPI Tables ===\n\n");

  // List all tables from RSDT/XSDT
  if (platform->rsdp->revision >= 2 && platform->rsdp->xsdt_address != 0) {
    struct acpi_xsdt *xsdt =
        (struct acpi_xsdt *)(uintptr_t)platform->rsdp->xsdt_address;
    uint32_t entries =
        (xsdt->header.length - sizeof(struct acpi_table_header)) / 8;

    printk("Using XSDT (");
    printk_dec(entries);
    printk(" tables)\n\n");

    for (uint32_t i = 0; i < entries; i++) {
      struct acpi_table_header *header =
          (struct acpi_table_header *)(uintptr_t)xsdt->entry[i];

      printk("  [");
      printk_dec(i);
      printk("] ");
      for (int j = 0; j < 4; j++) {
        char c = header->signature[j];
        if (c >= 32 && c <= 126) {
          printk_putc(c);
        } else {
          printk("?");
        }
      }
      printk("\n      Address: ");
      printk_hex64((uint64_t)header);
      printk("\n      Length: ");
      printk_dec(header->length);
      printk(" bytes\n      Revision: ");
      printk_dec(header->revision);
      printk("\n      OEM ID: ");
      for (int j = 0; j < 6; j++) {
        char c = header->oem_id[j];
        printk_putc(c >= 32 && c <= 126 ? c : '?');
      }
      printk("\n");

      // Add detailed logging for specific table types
      if (memcmp(header->signature, "APIC", 4) == 0) {
        dump_madt_details(header);
      }
      printk("\n");
    }
  } else if (platform->rsdp->rsdt_address != 0) {
    struct acpi_rsdt *rsdt =
        (struct acpi_rsdt *)(uintptr_t)platform->rsdp->rsdt_address;
    uint32_t entries =
        (rsdt->header.length - sizeof(struct acpi_table_header)) / 4;

    printk("Using RSDT (");
    printk_dec(entries);
    printk(" tables)\n\n");

    for (uint32_t i = 0; i < entries; i++) {
      struct acpi_table_header *header =
          (struct acpi_table_header *)(uintptr_t)rsdt->entry[i];

      printk("  [");
      printk_dec(i);
      printk("] ");
      for (int j = 0; j < 4; j++) {
        char c = header->signature[j];
        if (c >= 32 && c <= 126) {
          printk_putc(c);
        } else {
          printk("?");
        }
      }
      printk("\n      Address: ");
      printk_hex32((uint32_t)(uintptr_t)header);
      printk("\n      Length: ");
      printk_dec(header->length);
      printk(" bytes\n      Revision: ");
      printk_dec(header->revision);
      printk("\n      OEM ID: ");
      for (int j = 0; j < 6; j++) {
        char c = header->oem_id[j];
        printk_putc(c >= 32 && c <= 126 ? c : '?');
      }
      printk("\n");

      // Add detailed logging for specific table types
      if (memcmp(header->signature, "APIC", 4) == 0) {
        dump_madt_details(header);
      }
      printk("\n");
    }
  }

  printk("\n");
}

// Read RAM size from fw_cfg
uint64_t fw_cfg_read_ram_size(void) {
  outw(FW_CFG_PORT_SEL, FW_CFG_RAM_SIZE);

  uint64_t size = 0;
  for (int i = 0; i < 8; i++) {
    size |= ((uint64_t)inb(FW_CFG_PORT_DATA)) << (i * 8);
  }

  return size;
}

// Read number of CPUs from fw_cfg
uint32_t fw_cfg_read_nb_cpus(void) {
  outw(FW_CFG_PORT_SEL, FW_CFG_NB_CPUS);

  uint32_t cpus = 0;
  for (int i = 0; i < 4; i++) {
    cpus |= ((uint32_t)inb(FW_CFG_PORT_DATA)) << (i * 8);
  }

  return cpus;
}

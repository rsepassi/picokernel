// ACPI (Advanced Configuration and Power Interface) implementation for x64
// Provides device discovery by parsing ACPI tables
// x64 version uses 64-bit pointers and XSDT

#include "acpi.h"
#include "io.h"
#include "platform.h"
#include "platform_impl.h"
#include "printk.h"
#include "pvh.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

// Cache for DSDT pointer (DSDT is excluded from RSDT but we still need access)
static struct acpi_table_header *cached_dsdt = NULL;


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

      // Cache DSDT for later retrieval (it's not added to RSDT)
      if (memcmp(header->signature, "DSDT", 4) == 0) {
        cached_dsdt = header;
      }

      // Skip FACS, DSDT (referenced from FADT), and RSDT (we'll build our own)
      if (memcmp(header->signature, "FACS", 4) != 0 &&
          memcmp(header->signature, "DSDT", 4) != 0 &&
          memcmp(header->signature, "RSDT", 4) != 0) {
        // x32: Use 32-bit pointer cast
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
    // x32: Use 32-bit pointer cast
    rsdp->rsdt_address = (uint32_t)(uintptr_t)rsdt_buffer;
    // Set revision to 0 to indicate ACPI 1.0 (RSDT only, no XSDT)
    rsdp->revision = 0;
  }

  return (rsdp && tables_size > 0) ? rsdp : NULL;
}

// Find RSDP using fw_cfg (for QEMU microvm)
void *acpi_find_rsdp(void) {
  // Use fw_cfg only - no BIOS memory scanning
  // This ensures we load ACPI tables from fw_cfg which includes DSDT caching
  return fw_cfg_find_rsdp();
}

// Find a specific ACPI table by signature
struct acpi_table_header *acpi_find_table(platform_t *platform,
                                          const char *signature) {
  if (platform->rsdp == NULL) {
    return NULL;
  }

  // Special case: Return cached DSDT (not in RSDT)
  if (memcmp(signature, ACPI_SIG_DSDT, 4) == 0) {
    return cached_dsdt;
  }

  // x32: Only use RSDT (32-bit pointers), no XSDT support
  if (platform->rsdp->rsdt_address != 0) {
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
  // Find RSDP - check PVH boot info first, then fall back to fw_cfg
  if (platform->pvh_info && platform->pvh_info->rsdp_paddr != 0) {
    // Use RSDP address provided by PVH boot protocol
    platform->rsdp =
        (struct acpi_rsdp *)(uintptr_t)platform->pvh_info->rsdp_paddr;
  } else {
    // Fall back to fw_cfg for legacy boot
    platform->rsdp = (struct acpi_rsdp *)acpi_find_rsdp();
  }

  if (platform->rsdp == NULL) {
    printk("ACPI: RSDP not found (neither PVH nor fw_cfg provided it)\n");
    return;
  }

  KDEBUG_LOG("ACPI initialized (RSDP at 0x%x, Revision %d)",
             (uint32_t)(uintptr_t)platform->rsdp, platform->rsdp->revision);

  // Cache DSDT from FADT (for BIOS path where fw_cfg isn't used)
  struct acpi_table_header *fadt = acpi_find_table(platform, ACPI_SIG_FADT);
  if (fadt != NULL) {
    struct acpi_fadt *fadt_table = (struct acpi_fadt *)fadt;
    KDEBUG_LOG("FADT found, dsdt field = 0x%x", fadt_table->dsdt);
    if (fadt_table->dsdt != 0) {
      cached_dsdt = (struct acpi_table_header *)(uintptr_t)fadt_table->dsdt;
      KDEBUG_LOG("DSDT cached at 0x%x", (uint32_t)(uintptr_t)cached_dsdt);
    }
  } else {
    KDEBUG_LOG("FADT not found in RSDT");
  }
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

  // List all tables from RSDT (x32: no XSDT support)
  if (platform->rsdp->rsdt_address != 0) {
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

// Find virtio-mmio devices in ACPI DSDT
// Searches for "LNRO0005" devices and extracts MMIO base/size and IRQ
int acpi_find_virtio_mmio_devices(platform_t *platform,
                                   acpi_virtio_mmio_device_t *devices,
                                   int max_devices) {
  // Get DSDT table
  struct acpi_table_header *dsdt = acpi_find_table(platform, ACPI_SIG_DSDT);
  if (dsdt == NULL) {
    KDEBUG_LOG("DSDT not found");
    return 0;
  }

  KDEBUG_LOG("Scanning DSDT for virtio-mmio devices (LNRO0005)...");

  uint8_t *data = (uint8_t *)dsdt;
  uint32_t length = dsdt->length;
  int device_count = 0;

  // Search for "LNRO0005" string in DSDT
  for (uint32_t i = 0; i < length - 8 && device_count < max_devices; i++) {
    // Look for the HID string "LNRO0005"
    if (memcmp(&data[i], "LNRO0005", 8) == 0) {
      KDEBUG_LOG("  Found LNRO0005 at offset 0x%x", i);

      // Search forward for Memory32Fixed resource (0x86 opcode)
      // and Interrupt resource (0x89 opcode)
      uint64_t mmio_base = 0;
      uint32_t mmio_size = 0;
      uint32_t irq = 0;
      bool found_memory = false;
      bool found_interrupt = false;

      // Search next 512 bytes for resource descriptors
      for (uint32_t j = i; j < i + 512 && j < length - 10; j++) {
        // Memory32Fixed: 0x86 followed by 9 bytes
        // Format: 0x86 <len_lo> <len_hi> <flags> <base_lo> <base_hi> <size_lo> <size_hi>
        if (data[j] == 0x86 && !found_memory) {
          // Extract base address (little-endian, 32-bit)
          mmio_base = data[j + 4] | (data[j + 5] << 8) | (data[j + 6] << 16) |
                      ((uint32_t)data[j + 7] << 24);
          // Extract size (little-endian, 32-bit)
          mmio_size = data[j + 8] | (data[j + 9] << 8) | (data[j + 10] << 16) |
                      ((uint32_t)data[j + 11] << 24);
          found_memory = true;
          KDEBUG_LOG("    Memory32Fixed: base=0x%llx size=0x%x",
                     (unsigned long long)mmio_base, mmio_size);
        }

        // Extended Interrupt Descriptor: 0x89 followed by variable length
        // Format: 0x89 <len_lo> <len_hi> <flags> <int_count> <int_value>...
        if (data[j] == 0x89 && !found_interrupt) {
          // Skip 2 bytes (length field), 1 byte (flags), 1 byte (int count)
          // IRQ number is at offset j+5 (32-bit little-endian)
          if (j + 8 < length) {
            irq = data[j + 5] | (data[j + 6] << 8) | (data[j + 7] << 16) |
                  ((uint32_t)data[j + 8] << 24);
            found_interrupt = true;
            KDEBUG_LOG("    Interrupt: IRQ %u", irq);
          }
        }

        // If we found both, we're done with this device
        if (found_memory && found_interrupt) {
          devices[device_count].mmio_base = mmio_base;
          devices[device_count].mmio_size = mmio_size;
          devices[device_count].irq = irq;
          devices[device_count].valid = true;
          device_count++;
          break;
        }
      }

      // Warn if we found device but not all resources
      if (found_memory && !found_interrupt) {
        KDEBUG_LOG("    WARNING: Found memory but no interrupt resource");
      } else if (!found_memory && found_interrupt) {
        KDEBUG_LOG("    WARNING: Found interrupt but no memory resource");
      }
    }
  }

  KDEBUG_LOG("Found %d virtio-mmio device(s)", device_count);
  return device_count;
}

// Discover MMIO devices via ACPI (platform.h contract)
// Probes ACPI DSDT for virtio-mmio devices and reads their device IDs
int platform_discover_mmio_devices(platform_t *platform,
                                   platform_mmio_device_t *devices,
                                   int max_devices) {
  // Use ACPI to find virtio-mmio device descriptors
  acpi_virtio_mmio_device_t acpi_devices[MAX_VIRTIO_MMIO_DEVICES];
  int acpi_device_count = acpi_find_virtio_mmio_devices(
      platform, acpi_devices,
      max_devices < MAX_VIRTIO_MMIO_DEVICES ? max_devices : MAX_VIRTIO_MMIO_DEVICES);

  int valid_count = 0;

  // Probe each ACPI-discovered device and read its device ID from MMIO
  for (int i = 0; i < acpi_device_count && valid_count < max_devices; i++) {
    if (!acpi_devices[i].valid) {
      continue;
    }

    uint64_t base = acpi_devices[i].mmio_base;
    uint32_t irq_num = acpi_devices[i].irq;

    // Read magic value at offset 0x00
    volatile uint32_t *magic_ptr = (volatile uint32_t *)base;
    uint32_t magic = *magic_ptr;

    // VirtIO magic value is 0x74726976 ("virt" in little-endian)
    if (magic != 0x74726976) {
      continue; // Not a valid VirtIO device
    }

    // Read device ID at offset 0x08
    volatile uint32_t *device_id_ptr = (volatile uint32_t *)(base + 0x08);
    uint32_t device_id = *device_id_ptr;

    // Device ID 0 means empty slot
    if (device_id == 0) {
      continue;
    }

    // Fill platform-independent device descriptor
    devices[valid_count].mmio_base = base;
    devices[valid_count].mmio_size = acpi_devices[i].mmio_size;
    devices[valid_count].irq_num = irq_num;
    devices[valid_count].device_id = device_id;
    devices[valid_count].valid = true;
    valid_count++;
  }

  return valid_count;
}

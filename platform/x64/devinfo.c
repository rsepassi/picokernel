// x64 Device Enumeration using ACPI
// Uses ACPI tables to dynamically discover devices on x64 platforms

#include "acpi.h"
#include "kernel.h"
#include "printk.h"

// Parse and display MADT (Multiple APIC Description Table)
static void parse_madt(void) {
  struct acpi_table_header *header = acpi_find_table(ACPI_SIG_MADT);

  if (header == NULL) {
    printk("  MADT table not found\n\n");
    return;
  }

  struct acpi_madt *madt = (struct acpi_madt *)header;

  printk("  MADT (Multiple APIC Description Table) found\n");
  printk("  Local APIC Address: ");
  printk_hex32(madt->local_apic_address);
  printk("\n");
  printk("  Flags: ");
  printk_hex32(madt->flags);
  printk("\n\n");

  // Parse MADT entries
  uint8_t *ptr = (uint8_t *)madt + sizeof(struct acpi_madt);
  uint8_t *end = (uint8_t *)madt + madt->header.length;

  uint32_t cpu_count = 0;
  uint32_t ioapic_count = 0;

  printk("  cpus {\n");
  printk("    #address-cells = <1>;\n");
  printk("    #size-cells = <0>;\n\n");

  while (ptr < end) {
    struct acpi_madt_entry_header *entry = (struct acpi_madt_entry_header *)ptr;

    if (entry->type == ACPI_MADT_TYPE_LOCAL_APIC) {
      struct acpi_madt_local_apic *lapic = (struct acpi_madt_local_apic *)entry;

      // Only show enabled CPUs
      if (lapic->flags & 1) {
        printk("    cpu@");
        printk_dec(cpu_count);
        printk(" {\n");
        printk("      device_type = \"cpu\";\n");
        printk("      compatible = \"intel,x86_64\";\n");
        printk("      reg = <");
        printk_dec(lapic->apic_id);
        printk(">;\n");
        printk("      acpi-processor-id = <");
        printk_dec(lapic->acpi_processor_id);
        printk(">;\n");
        printk("    };\n");
        cpu_count++;
      }
    }

    ptr += entry->length;
  }

  printk("  };\n\n");

  // Parse I/O APICs
  ptr = (uint8_t *)madt + sizeof(struct acpi_madt);

  while (ptr < end) {
    struct acpi_madt_entry_header *entry = (struct acpi_madt_entry_header *)ptr;

    if (entry->type == ACPI_MADT_TYPE_IO_APIC) {
      struct acpi_madt_io_apic *ioapic = (struct acpi_madt_io_apic *)entry;

      printk("  ioapic@");
      printk_hex32(ioapic->io_apic_address);
      printk(" {\n");
      printk("    compatible = \"intel,ioapic\";\n");
      printk("    device_type = \"interrupt-controller\";\n");
      printk("    reg = <0x00000000 ");
      printk_hex32(ioapic->io_apic_address);
      printk(" 0x00000000 0x00001000>;\n");
      printk("    ioapic-id = <");
      printk_dec(ioapic->io_apic_id);
      printk(">;\n");
      printk("    global-irq-base = <");
      printk_dec(ioapic->global_irq_base);
      printk(">;\n");
      printk("  };\n\n");
      ioapic_count++;
    }

    ptr += entry->length;
  }

  printk("  Summary: ");
  printk_dec(cpu_count);
  printk(" CPU(s), ");
  printk_dec(ioapic_count);
  printk(" I/O APIC(s)\n\n");
}

// Helper to print a 64-bit hex value split into two 32-bit parts (for device
// tree)
static void print_dt_reg64(uint64_t val) {
  printk_hex32((uint32_t)(val >> 32));
  printk(" ");
  printk_hex32((uint32_t)(val & 0xFFFFFFFF));
}

// x64-specific device enumeration
// This function provides the same interface as fdt_dump() but enumerates
// x64 devices using ACPI tables
void fdt_dump(void *dummy) {
  (void)dummy;
  printk("=== x64 Device Enumeration ===\n\n");

  // Initialize ACPI subsystem
  acpi_init();

  // Dump available ACPI tables
  acpi_dump_tables();

  printk("=== Device Tree ===\n\n");

  // Get platform information from ACPI
  struct acpi_table_header *fadt = acpi_find_table(ACPI_SIG_FADT);

  printk("Platform: x86_64\n");
  printk("Device Discovery: ACPI\n");
  if (fadt != NULL) {
    printk("Firmware OEM: ");
    for (int i = 0; i < 6; i++) {
      char c = fadt->oem_id[i];
      if (c >= 32 && c <= 126) {
        printk_putc(c);
      }
    }
    printk("\n");
  }
  printk("\n");

  printk("/ {\n");
  printk("  model = \"x86_64\";\n");
  printk("  compatible = \"intel,x86_64\";\n");
  printk("  #address-cells = <2>;\n");
  printk("  #size-cells = <2>;\n\n");

  // Parse MADT for CPU and interrupt controller information
  parse_madt();

  // Memory node - discover from fw_cfg
  uint64_t ram_size = fw_cfg_read_ram_size();
  printk("  memory@0 {\n");
  printk("    device_type = \"memory\";\n");
  printk("    reg = <");
  print_dt_reg64(0); // Start address
  printk(" ");
  print_dt_reg64(ram_size); // Size
  printk(">; // ");
  printk_dec((uint32_t)(ram_size / (1024 * 1024)));
  printk(" MiB\n");
  printk("  };\n\n");

  // Serial console - detect from I/O port probe
  // Standard PC COM1 port (0x3f8) - check if present by reading LSR
  uint16_t serial_base = 0x3f8;
  printk("  serial@");
  printk_hex32(serial_base);
  printk(" {\n");
  printk("    compatible = \"ns16550a\";\n");
  printk("    device_type = \"serial\";\n");
  printk("    reg = <");
  print_dt_reg64(serial_base);
  printk(" ");
  print_dt_reg64(8); // 8 byte range
  printk(">;\n");
  printk("    io-port = <");
  printk_hex32(serial_base);
  printk(">;\n");
  printk("    irq = <4>;\n");
  printk("    clock-frequency = <1843200>;\n");
  printk("  };\n\n");

  printk("  chosen {\n");
  printk("    bootargs = \"\";\n");
  printk("    stdout-path = \"/serial@");
  printk_hex32(serial_base);
  printk("\";\n");
  printk("  };\n");

  printk("};\n\n");

  printk("=== End Device Enumeration ===\n\n");
}

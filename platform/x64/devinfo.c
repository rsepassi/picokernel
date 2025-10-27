// x64 Device Enumeration using ACPI
// Uses ACPI tables to dynamically discover devices on x64 platforms

#include <stdint.h>
#include "acpi.h"

#define NULL ((void*)0)

// Forward declaration of uart_puts from platform-specific uart.c
void uart_puts(const char* str);
void uart_putc(char);


// Helper to convert uint32 to hex string
void print_hex32(uint32_t val)
{
    const char hex[] = "0123456789abcdef";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xf]);
    }
}

// Helper to convert uint64 to hex string
void print_hex64(uint64_t val)
{
    const char hex[] = "0123456789abcdef";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xf]);
    }
}

// Helper function to print decimal values
void uart_putdec(uint32_t value)
{
    char buffer[11];
    int i = 0;

    if (value == 0) {
        uart_puts("0");
        return;
    }

    while (value > 0) {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }

    for (int j = 0; j < i / 2; j++) {
        char tmp = buffer[j];
        buffer[j] = buffer[i - 1 - j];
        buffer[i - 1 - j] = tmp;
    }
    buffer[i] = '\0';
    uart_puts(buffer);
}

// Parse and display MADT (Multiple APIC Description Table)
static void parse_madt(void)
{
    struct acpi_table_header* header = acpi_find_table(ACPI_SIG_MADT);

    if (header == NULL) {
        uart_puts("  MADT table not found\n\n");
        return;
    }

    struct acpi_madt* madt = (struct acpi_madt*)header;

    uart_puts("  MADT (Multiple APIC Description Table) found\n");
    uart_puts("  Local APIC Address: ");
    print_hex32(madt->local_apic_address);
    uart_puts("\n");
    uart_puts("  Flags: ");
    print_hex32(madt->flags);
    uart_puts("\n\n");

    // Parse MADT entries
    uint8_t* ptr = (uint8_t*)madt + sizeof(struct acpi_madt);
    uint8_t* end = (uint8_t*)madt + madt->header.length;

    uint32_t cpu_count = 0;
    uint32_t ioapic_count = 0;

    uart_puts("  cpus {\n");
    uart_puts("    #address-cells = <1>;\n");
    uart_puts("    #size-cells = <0>;\n\n");

    while (ptr < end) {
        struct acpi_madt_entry_header* entry = (struct acpi_madt_entry_header*)ptr;

        if (entry->type == ACPI_MADT_TYPE_LOCAL_APIC) {
            struct acpi_madt_local_apic* lapic = (struct acpi_madt_local_apic*)entry;

            // Only show enabled CPUs
            if (lapic->flags & 1) {
                uart_puts("    cpu@");
                uart_putdec(cpu_count);
                uart_puts(" {\n");
                uart_puts("      device_type = \"cpu\";\n");
                uart_puts("      compatible = \"intel,x86_64\";\n");
                uart_puts("      reg = <");
                uart_putdec(lapic->apic_id);
                uart_puts(">;\n");
                uart_puts("      acpi-processor-id = <");
                uart_putdec(lapic->acpi_processor_id);
                uart_puts(">;\n");
                uart_puts("    };\n");
                cpu_count++;
            }
        }

        ptr += entry->length;
    }

    uart_puts("  };\n\n");

    // Parse I/O APICs
    ptr = (uint8_t*)madt + sizeof(struct acpi_madt);

    while (ptr < end) {
        struct acpi_madt_entry_header* entry = (struct acpi_madt_entry_header*)ptr;

        if (entry->type == ACPI_MADT_TYPE_IO_APIC) {
            struct acpi_madt_io_apic* ioapic = (struct acpi_madt_io_apic*)entry;

            uart_puts("  ioapic@");
            print_hex32(ioapic->io_apic_address);
            uart_puts(" {\n");
            uart_puts("    compatible = \"intel,ioapic\";\n");
            uart_puts("    device_type = \"interrupt-controller\";\n");
            uart_puts("    reg = <0x00000000 ");
            print_hex32(ioapic->io_apic_address);
            uart_puts(" 0x00000000 0x00001000>;\n");
            uart_puts("    ioapic-id = <");
            uart_putdec(ioapic->io_apic_id);
            uart_puts(">;\n");
            uart_puts("    global-irq-base = <");
            uart_putdec(ioapic->global_irq_base);
            uart_puts(">;\n");
            uart_puts("  };\n\n");
            ioapic_count++;
        }

        ptr += entry->length;
    }

    uart_puts("  Summary: ");
    uart_putdec(cpu_count);
    uart_puts(" CPU(s), ");
    uart_putdec(ioapic_count);
    uart_puts(" I/O APIC(s)\n\n");
}

// Helper to print a 64-bit hex value split into two 32-bit parts (for device tree)
static void print_dt_reg64(uint64_t val)
{
    print_hex32((uint32_t)(val >> 32));
    uart_puts(" ");
    print_hex32((uint32_t)(val & 0xFFFFFFFF));
}

// x64-specific device enumeration
// This function provides the same interface as fdt_dump() but enumerates
// x64 devices using ACPI tables
void fdt_dump(void* dummy)
{
  (void)dummy;
    uart_puts("=== x64 Device Enumeration ===\n\n");

    // Initialize ACPI subsystem
    acpi_init();

    // Dump available ACPI tables
    acpi_dump_tables();

    uart_puts("=== Device Tree ===\n\n");

    // Get platform information from ACPI
    struct acpi_table_header* fadt = acpi_find_table(ACPI_SIG_FADT);

    uart_puts("Platform: x86_64\n");
    uart_puts("Device Discovery: ACPI\n");
    if (fadt != NULL) {
        uart_puts("Firmware OEM: ");
        for (int i = 0; i < 6; i++) {
            char c = fadt->oem_id[i];
            if (c >= 32 && c <= 126) {
                uart_putc(c);
            }
        }
        uart_puts("\n");
    }
    uart_puts("\n");

    uart_puts("/ {\n");
    uart_puts("  model = \"x86_64\";\n");
    uart_puts("  compatible = \"intel,x86_64\";\n");
    uart_puts("  #address-cells = <2>;\n");
    uart_puts("  #size-cells = <2>;\n\n");

    // Parse MADT for CPU and interrupt controller information
    parse_madt();

    // Memory node - discover from fw_cfg
    uint64_t ram_size = fw_cfg_read_ram_size();
    uart_puts("  memory@0 {\n");
    uart_puts("    device_type = \"memory\";\n");
    uart_puts("    reg = <");
    print_dt_reg64(0);  // Start address
    uart_puts(" ");
    print_dt_reg64(ram_size);  // Size
    uart_puts(">; // ");
    uart_putdec((uint32_t)(ram_size / (1024 * 1024)));
    uart_puts(" MiB\n");
    uart_puts("  };\n\n");

    // Serial console - detect from I/O port probe
    // Standard PC COM1 port (0x3f8) - check if present by reading LSR
    uint16_t serial_base = 0x3f8;
    uart_puts("  serial@");
    print_hex32(serial_base);
    uart_puts(" {\n");
    uart_puts("    compatible = \"ns16550a\";\n");
    uart_puts("    device_type = \"serial\";\n");
    uart_puts("    reg = <");
    print_dt_reg64(serial_base);
    uart_puts(" ");
    print_dt_reg64(8);  // 8 byte range
    uart_puts(">;\n");
    uart_puts("    io-port = <");
    print_hex32(serial_base);
    uart_puts(">;\n");
    uart_puts("    irq = <4>;\n");
    uart_puts("    clock-frequency = <1843200>;\n");
    uart_puts("  };\n\n");

    uart_puts("  chosen {\n");
    uart_puts("    bootargs = \"\";\n");
    uart_puts("    stdout-path = \"/serial@");
    print_hex32(serial_base);
    uart_puts("\";\n");
    uart_puts("  };\n");

    uart_puts("};\n\n");

    uart_puts("=== End Device Enumeration ===\n\n");
}

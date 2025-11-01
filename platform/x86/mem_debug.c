// x86 Memory Debugging Utilities
// Implementation of memory validation and debugging tools

#include "mem_debug.h"
#include "platform.h"
#include "printk.h"
#include <stddef.h>

// External symbols from linker script
extern char __bss_start[];
extern char __bss_end[];
extern char _end[];

// Helper to print a pointer address
static void printk_ptr(const void *ptr) {
    printk_hex64((uint64_t)(uintptr_t)ptr);
}

// Helper to print decimal as 64-bit (printk_dec is 32-bit)
static void printk_dec64(uint64_t val) {
    if (val > UINT32_MAX) {
        printk_dec((uint32_t)(val / 1000000000));
        uint32_t remainder = (uint32_t)(val % 1000000000);
        // Print leading zeros for remainder
        if (remainder < 100000000) printk_putc('0');
        if (remainder < 10000000) printk_putc('0');
        if (remainder < 1000000) printk_putc('0');
        if (remainder < 100000) printk_putc('0');
        if (remainder < 10000) printk_putc('0');
        if (remainder < 1000) printk_putc('0');
        if (remainder < 100) printk_putc('0');
        if (remainder < 10) printk_putc('0');
        printk_dec(remainder);
    } else {
        printk_dec((uint32_t)val);
    }
}

// Dump memory region to console (hex dump)
void mem_dump(const void *addr, uint64_t len) {
    const uint8_t *p = (const uint8_t *)addr;

    for (uint64_t i = 0; i < len; i += 16) {
        printk_ptr((const void *)((uintptr_t)addr + i));
        printk(": ");

        // Print hex bytes
        for (uint64_t j = 0; j < 16 && i + j < len; j++) {
            printk_hex8(p[i + j]);
            printk_putc(' ');
            if (j == 7) printk_putc(' ');
        }

        // Pad if less than 16 bytes
        for (uint64_t j = len - i; j < 16; j++) {
            printk("   ");
            if (j == 7) printk_putc(' ');
        }

        // Print ASCII representation
        printk(" |");
        for (uint64_t j = 0; j < 16 && i + j < len; j++) {
            uint8_t c = p[i + j];
            printk_putc((c >= 32 && c < 127) ? c : '.');
        }
        printk("|\n");
    }
}

// Dump a specific memory region by address range
void mem_dump_range(const char *label, uint64_t start, uint64_t end) {
    uint64_t size = end - start;
    printk("\n[MEM] ");
    printk(label);
    printk(" (");
    printk_hex64(start);
    printk(" - ");
    printk_hex64(end);
    printk(", ");
    printk_dec64(size);
    printk(" bytes):\n");

    // Limit dump size to avoid overwhelming output
    if (size > 256) {
        printk("  First 256 bytes:\n");
        mem_dump((const void *)start, 256);
        printk("  ... (truncated ");
        printk_dec64(size - 256);
        printk(" bytes)\n");
    } else {
        mem_dump((const void *)start, size);
    }
}

// Validate that a memory region contains expected pattern
bool mem_validate_pattern(const void *addr, uint64_t len, uint8_t pattern) {
    const uint8_t *p = (const uint8_t *)addr;

    for (uint64_t i = 0; i < len; i++) {
        if (p[i] != pattern) {
            printk("[MEM] Pattern mismatch at ");
            printk_ptr((const void *)((uintptr_t)addr + i));
            printk(": expected ");
            printk_hex8(pattern);
            printk(", got ");
            printk_hex8(p[i]);
            printk("\n");
            return false;
        }
    }

    return true;
}

// Check if an address range overlaps with another
bool mem_ranges_overlap(uint64_t a_start, uint64_t a_end,
                        uint64_t b_start, uint64_t b_end) {
    return !(a_end <= b_start || b_end <= a_start);
}

// Dump page table contents
void mem_dump_page_tables(void) {
    printk("\n[MEM] Page Table Dump:\n");

    // PML4 at 0x100000
    printk("  PML4 (0x100000):\n");
    mem_dump((const void *)0x100000, 64);

    // PDPT at 0x101000
    printk("\n  PDPT (0x101000):\n");
    mem_dump((const void *)0x101000, 64);

    // PD0 at 0x102000 (first few entries)
    printk("\n  PD0 (0x102000) - First 4 entries:\n");
    mem_dump((const void *)0x102000, 32);

    // PD3 at 0x103000 (MMIO region, first few entries)
    printk("\n  PD3 (0x103000) - First 4 entries:\n");
    mem_dump((const void *)0x103000, 32);

    // PT at 0x104000 (kernel region, first few entries)
    printk("\n  PT (0x104000) - First 8 entries:\n");
    mem_dump((const void *)0x104000, 64);
}

// Validate a specific memory region
bool mem_validate_region(const mem_region_t *region) {
    printk("[MEM] Validating ");
    printk(region->name);
    printk(" (");
    printk_hex64(region->base);
    printk(" - ");
    printk_hex64(region->base + region->size);
    printk(", ");
    printk_dec64(region->size);
    printk(" bytes, ");
    printk(region->writable ? "R/W" : "R/O");
    printk(")\n");

    // For read-only regions, try to detect if they look corrupted
    // We can't actually test write protection without faulting
    if (!region->writable) {
        // Check if the region contains mostly null bytes (likely corrupted)
        const uint8_t *p = (const uint8_t *)region->base;
        uint64_t null_count = 0;
        uint64_t sample_size = region->size < 256 ? region->size : 256;

        for (uint64_t i = 0; i < sample_size; i++) {
            if (p[i] == 0) null_count++;
        }

        // If more than 90% nulls in a non-BSS section, suspicious
        if (region->base < MEM_REGION_BSS_BASE &&
            (null_count * 100 / sample_size) > 90) {
            printk("[MEM] WARNING: ");
            printk(region->name);
            printk(" appears corrupted (>90% null bytes)\n");
            return false;
        }
    }

    return true;
}

// Validate critical memory regions
bool mem_validate_critical_regions(void) {
    bool all_ok = true;

    printk("\n[MEM] === Memory Region Validation ===\n");

    // Define critical regions
    static const mem_region_t regions[] = {
        {"Page Tables", MEM_REGION_PAGE_TABLES_BASE, MEM_REGION_PAGE_TABLES_SIZE, true},
        {".text", MEM_REGION_KERNEL_BASE, MEM_REGION_KERNEL_TEXT_END - MEM_REGION_KERNEL_BASE, false},
        {".rodata", MEM_REGION_RODATA_BASE, MEM_REGION_RODATA_END - MEM_REGION_RODATA_BASE, false},
        {".bss", MEM_REGION_BSS_BASE, MEM_REGION_KERNEL_END - MEM_REGION_BSS_BASE, true},
    };

    for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); i++) {
        if (!mem_validate_region(&regions[i])) {
            all_ok = false;
        }
    }

    // Check for overlaps
    printk("\n[MEM] Checking for region overlaps:\n");
    for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); i++) {
        for (size_t j = i + 1; j < sizeof(regions) / sizeof(regions[0]); j++) {
            uint64_t a_end = regions[i].base + regions[i].size;
            uint64_t b_end = regions[j].base + regions[j].size;

            if (mem_ranges_overlap(regions[i].base, a_end, regions[j].base, b_end)) {
                printk("[MEM] ERROR: ");
                printk(regions[i].name);
                printk(" overlaps with ");
                printk(regions[j].name);
                printk("\n");
                all_ok = false;
            }
        }
    }

    printk("\n[MEM] === Validation ");
    printk(all_ok ? "PASSED" : "FAILED");
    printk(" ===\n\n");
    return all_ok;
}

// Print memory map summary
void mem_print_map(void) {
    printk("\n[MEM] === x86 Memory Map ===\n");
    printk("  BIOS/Low Memory:     0x00000000 - 0x000FFFFF (1 MiB)\n");

    printk("  Page Tables:         ");
    printk_hex64(MEM_REGION_PAGE_TABLES_BASE);
    printk(" - ");
    printk_hex64(MEM_REGION_PAGE_TABLES_BASE + MEM_REGION_PAGE_TABLES_SIZE);
    printk(" (");
    printk_dec(MEM_REGION_PAGE_TABLES_SIZE / 1024);
    printk(" KiB)\n");

    printk("  Kernel .text:        ");
    printk_hex64(MEM_REGION_KERNEL_BASE);
    printk(" - ");
    printk_hex64(MEM_REGION_KERNEL_TEXT_END);
    printk(" (");
    printk_dec((MEM_REGION_KERNEL_TEXT_END - MEM_REGION_KERNEL_BASE) / 1024);
    printk(" KiB)\n");

    printk("  Kernel .rodata:      ");
    printk_hex64(MEM_REGION_RODATA_BASE);
    printk(" - ");
    printk_hex64(MEM_REGION_RODATA_END);
    printk(" (");
    printk_dec((MEM_REGION_RODATA_END - MEM_REGION_RODATA_BASE) / 1024);
    printk(" KiB)\n");

    printk("  Kernel .bss+stack:   ");
    printk_hex64(MEM_REGION_BSS_BASE);
    printk(" - ");
    printk_hex64(MEM_REGION_KERNEL_END);
    printk(" (");
    printk_dec((MEM_REGION_KERNEL_END - MEM_REGION_BSS_BASE) / 1024);
    printk(" KiB)\n");

    printk("  Free RAM:            ");
    printk_hex64(MEM_REGION_KERNEL_END);
    printk(" - 0x08000000 (~");
    printk_dec((0x08000000 - MEM_REGION_KERNEL_END) / (1024 * 1024));
    printk(" MiB)\n");

    printk("  PCI MMIO:            0xC0000000 - 0xD0000000 (256 MiB)\n");
    printk("  High MMIO:           0xFE000000 - 0xFF000000 (16 MiB)\n");
    printk("    - IOAPIC:          0xFEC00000\n");
    printk("    - Local APIC:      0xFEE00000\n");
    printk("\n");
}

// Add a memory guard/canary value at a specific address
void mem_set_guard(void *addr, uint64_t pattern) {
    *(uint64_t *)addr = pattern;
}

// Check if a memory guard is intact
bool mem_check_guard(const void *addr, uint64_t pattern) {
    uint64_t value = *(const uint64_t *)addr;
    if (value != pattern) {
        printk("[MEM] GUARD FAILED at ");
        printk_ptr(addr);
        printk(": expected ");
        printk_hex64(pattern);
        printk(", got ");
        printk_hex64(value);
        printk("\n");
        return false;
    }
    return true;
}

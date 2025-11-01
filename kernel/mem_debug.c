#include "mem_debug.h"
#include "kbase.h"
#include "printk.h"
#include "crc32.h"

#ifdef KDEBUG

// Hex dump with ASCII visualization
void kmem_dump(const void* addr, uint32_t len) {
    const uint8_t* p = (const uint8_t*)addr;

    for (uint32_t i = 0; i < len; i += 16) {
        // Address
        printk("  0x");
        printk_hex64((uintptr_t)(p + i));
        printk(": ");

        // Hex bytes
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < len) {
                printk_hex8(p[i + j]);
                printk(" ");
            } else {
                printk("   ");
            }
        }

        printk(" ");

        // ASCII representation
        for (uint32_t j = 0; j < 16 && i + j < len; j++) {
            uint8_t c = p[i + j];
            if (c >= 32 && c <= 126) {
                printk_putc(c);
            } else {
                printk(".");
            }
        }

        printk("\n");
    }
}

// Dump memory range with label
void kmem_dump_range(const char* label, const void* start, const void* end) {
    uintptr_t s = (uintptr_t)start;
    uintptr_t e = (uintptr_t)end;

    if (e < s) {
        printk("Invalid range: end < start\n");
        return;
    }

    uint32_t len = (uint32_t)(e - s);

    printk(label);
    printk(" (0x");
    printk_hex64(s);
    printk(" - 0x");
    printk_hex64(e);
    printk(", ");
    printk_dec(len);
    printk(" bytes):\n");

    kmem_dump(start, len);
}

// Validate memory contains expected pattern
bool kmem_validate_pattern(const void* addr, uint32_t len, uint8_t pattern) {
    const uint8_t* p = (const uint8_t*)addr;

    for (uint32_t i = 0; i < len; i++) {
        if (p[i] != pattern) {
            printk("Pattern mismatch at offset ");
            printk_dec(i);
            printk(": expected 0x");
            printk_hex8(pattern);
            printk(", got 0x");
            printk_hex8(p[i]);
            printk("\n");
            return false;
        }
    }

    return true;
}

// Check if two memory ranges overlap
bool kmem_ranges_overlap(uintptr_t a_start, uint32_t a_size, uintptr_t b_start, uint32_t b_size) {
    uintptr_t a_end = a_start + a_size;
    uintptr_t b_end = b_start + b_size;

    // Ranges don't overlap if one ends before the other starts
    if (a_end <= b_start || b_end <= a_start) {
        return false;
    }

    return true;
}

// Compute CRC32 checksum of data (wrapper around crc32_compute)
uint32_t kmem_crc32(const void* data, uint32_t len) {
    return crc32_compute(data, len);
}

// Compute checksum of a memory section (convenience wrapper)
uint32_t kmem_checksum_section(const void* start, const void* end) {
    uintptr_t s = (uintptr_t)start;
    uintptr_t e = (uintptr_t)end;

    if (e <= s) {
        return 0;
    }

    uint32_t len = (uint32_t)(e - s);
    return kmem_crc32(start, len);
}

#endif // KDEBUG

#include "mem_debug.h"
#include "kbase.h"

#ifdef KDEBUG

// Hex dump with ASCII visualization
void kmem_dump(const void* addr, u32 len) {
    const u8* p = (const u8*)addr;

    for (u32 i = 0; i < len; i += 16) {
        // Address
        printks("  0x");
        printk_hex64((uptr)(p + i));
        printks(": ");

        // Hex bytes
        for (u32 j = 0; j < 16; j++) {
            if (i + j < len) {
                printk_hex8(p[i + j]);
                printks(" ");
            } else {
                printks("   ");
            }
        }

        printks(" ");

        // ASCII representation
        for (u32 j = 0; j < 16 && i + j < len; j++) {
            u8 c = p[i + j];
            if (c >= 32 && c <= 126) {
                printk_char(c);
            } else {
                printks(".");
            }
        }

        printks("\n");
    }
}

// Dump memory range with label
void kmem_dump_range(const char* label, const void* start, const void* end) {
    uptr s = (uptr)start;
    uptr e = (uptr)end;

    if (e < s) {
        printks("Invalid range: end < start\n");
        return;
    }

    u32 len = (u32)(e - s);

    printks(label);
    printks(" (0x");
    printk_hex64(s);
    printks(" - 0x");
    printk_hex64(e);
    printks(", ");
    printk_dec(len);
    printks(" bytes):\n");

    kmem_dump(start, len);
}

// Validate memory contains expected pattern
bool kmem_validate_pattern(const void* addr, u32 len, u8 pattern) {
    const u8* p = (const u8*)addr;

    for (u32 i = 0; i < len; i++) {
        if (p[i] != pattern) {
            printks("Pattern mismatch at offset ");
            printk_dec(i);
            printks(": expected 0x");
            printk_hex8(pattern);
            printks(", got 0x");
            printk_hex8(p[i]);
            printks("\n");
            return false;
        }
    }

    return true;
}

// Check if two memory ranges overlap
bool kmem_ranges_overlap(uptr a_start, u32 a_size, uptr b_start, u32 b_size) {
    uptr a_end = a_start + a_size;
    uptr b_end = b_start + b_size;

    // Ranges don't overlap if one ends before the other starts
    if (a_end <= b_start || b_end <= a_start) {
        return false;
    }

    return true;
}

#endif // KDEBUG

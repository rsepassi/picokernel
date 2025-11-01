// x86 Platform Memory Debugging Implementation
// Implements kernel/mem_debug.h interface for x86/x64

#include "kernel/mem_debug.h"
#include "platform.h"
#include "kbase.h"

#ifdef KDEBUG

// Memory region definitions for x86
#define MEM_REGION_PAGE_TABLES_BASE  0x100000
#define MEM_REGION_PAGE_TABLES_SIZE  0x5000
#define MEM_REGION_KERNEL_BASE       0x200000
#define MEM_REGION_KERNEL_TEXT_END   0x20D000
#define MEM_REGION_RODATA_BASE       0x20D000
#define MEM_REGION_RODATA_END        0x20F000
#define MEM_REGION_BSS_BASE          0x20F000
#define MEM_REGION_KERNEL_END        0x23DC00

// Memory region descriptor (x86-specific)
typedef struct {
    const char* name;
    uptr base;
    u32 size;
    bool writable;
} mem_region_t;

// Validate a specific memory region
static bool validate_region(const mem_region_t* region) {
    printks("[MEM] Validating ");
    printks(region->name);
    printks(" (0x");
    printk_hex64(region->base);
    printks(" - 0x");
    printk_hex64(region->base + region->size);
    printks(", ");
    printk_dec(region->size);
    printks(" bytes, ");
    printks(region->writable ? "R/W" : "R/O");
    printks(")\n");

    // For read-only regions, detect if they look corrupted
    if (!region->writable) {
        const u8* p = (const u8*)region->base;
        u32 null_count = 0;
        u32 sample_size = region->size < 256 ? region->size : 256;

        for (u32 i = 0; i < sample_size; i++) {
            if (p[i] == 0) null_count++;
        }

        // If more than 90% nulls in a non-BSS section, suspicious
        if (region->base < MEM_REGION_BSS_BASE &&
            (null_count * 100 / sample_size) > 90) {
            printks("[MEM] WARNING: ");
            printks(region->name);
            printks(" appears corrupted (>90% null bytes)\n");
            return false;
        }
    }

    return true;
}

// Validate critical memory regions (page tables, kernel sections)
void platform_mem_validate_critical(void) {
    bool all_ok = true;

    printks("\n[MEM] === x86 Memory Region Validation ===\n");

    // Define critical regions
    static const mem_region_t regions[] = {
        {"Page Tables", MEM_REGION_PAGE_TABLES_BASE, MEM_REGION_PAGE_TABLES_SIZE, true},
        {".text", MEM_REGION_KERNEL_BASE, MEM_REGION_KERNEL_TEXT_END - MEM_REGION_KERNEL_BASE, false},
        {".rodata", MEM_REGION_RODATA_BASE, MEM_REGION_RODATA_END - MEM_REGION_RODATA_BASE, false},
        {".bss", MEM_REGION_BSS_BASE, MEM_REGION_KERNEL_END - MEM_REGION_BSS_BASE, true},
    };

    for (u32 i = 0; i < ARRAY_SIZE(regions); i++) {
        if (!validate_region(&regions[i])) {
            all_ok = false;
        }
    }

    // Check for overlaps
    printks("\n[MEM] Checking for region overlaps:\n");
    for (u32 i = 0; i < ARRAY_SIZE(regions); i++) {
        for (u32 j = i + 1; j < ARRAY_SIZE(regions); j++) {
            uptr a_end = regions[i].base + regions[i].size;
            uptr b_end = regions[j].base + regions[j].size;

            if (kmem_ranges_overlap(regions[i].base, regions[i].size,
                                   regions[j].base, regions[j].size)) {
                printks("[MEM] ERROR: ");
                printks(regions[i].name);
                printks(" overlaps with ");
                printks(regions[j].name);
                printks("\n");
                all_ok = false;
            }
        }
    }

    printks("\n[MEM] === Validation ");
    printks(all_ok ? "PASSED" : "FAILED");
    printks(" ===\n\n");
}

// Validate post-initialization state
void platform_mem_validate_post_init(platform_t *platform, void *fdt) {
    (void)platform;
    (void)fdt;
    printks("\n[MEM] === x86 Post-Init Validation ===\n");
    printks("[MEM] x86 post-init validation not implemented yet\n");
    printks("[MEM] === Validation SKIPPED ===\n\n");
}

// Print x86 memory map
void platform_mem_print_layout(void) {
    printks("\n[MEM] === x86 Memory Map ===\n");
    printks("  BIOS/Low Memory:     0x00000000 - 0x000FFFFF (1 MiB)\n");

    printks("  Page Tables:         0x");
    printk_hex64(MEM_REGION_PAGE_TABLES_BASE);
    printks(" - 0x");
    printk_hex64(MEM_REGION_PAGE_TABLES_BASE + MEM_REGION_PAGE_TABLES_SIZE);
    printks(" (");
    printk_dec(MEM_REGION_PAGE_TABLES_SIZE / 1024);
    printks(" KiB)\n");

    printks("  Kernel .text:        0x");
    printk_hex64(MEM_REGION_KERNEL_BASE);
    printks(" - 0x");
    printk_hex64(MEM_REGION_KERNEL_TEXT_END);
    printks(" (");
    printk_dec((MEM_REGION_KERNEL_TEXT_END - MEM_REGION_KERNEL_BASE) / 1024);
    printks(" KiB)\n");

    printks("  Kernel .rodata:      0x");
    printk_hex64(MEM_REGION_RODATA_BASE);
    printks(" - 0x");
    printk_hex64(MEM_REGION_RODATA_END);
    printks(" (");
    printk_dec((MEM_REGION_RODATA_END - MEM_REGION_RODATA_BASE) / 1024);
    printks(" KiB)\n");

    printks("  Kernel .bss+stack:   0x");
    printk_hex64(MEM_REGION_BSS_BASE);
    printks(" - 0x");
    printk_hex64(MEM_REGION_KERNEL_END);
    printks(" (");
    printk_dec((MEM_REGION_KERNEL_END - MEM_REGION_BSS_BASE) / 1024);
    printks(" KiB)\n");

    printks("  Free RAM:            0x");
    printk_hex64(MEM_REGION_KERNEL_END);
    printks(" - 0x08000000 (~");
    printk_dec((0x08000000 - MEM_REGION_KERNEL_END) / (1024 * 1024));
    printks(" MiB)\n");

    printks("  PCI MMIO:            0xC0000000 - 0xD0000000 (256 MiB)\n");
    printks("  High MMIO:           0xFE000000 - 0xFF000000 (16 MiB)\n");
    printks("    - IOAPIC:          0xFEC00000\n");
    printks("    - Local APIC:      0xFEE00000\n");
    printks("\n");
}

// Dump virtual address translation (x86 page tables)
void platform_mem_dump_translation(uptr vaddr) {
    printks("\n[MEM] Virtual address translation for 0x");
    printk_hex64(vaddr);
    printks(":\n");

    // x86-64 4-level paging: PML4 → PDPT → PD → PT → Physical
    u32 pml4_idx = (vaddr >> 39) & 0x1FF;
    u32 pdpt_idx = (vaddr >> 30) & 0x1FF;
    u32 pd_idx = (vaddr >> 21) & 0x1FF;
    u32 pt_idx = (vaddr >> 12) & 0x1FF;
    u32 offset = vaddr & 0xFFF;

    printks("  PML4 index: ");
    printk_dec(pml4_idx);
    printks(", PDPT index: ");
    printk_dec(pdpt_idx);
    printks(", PD index: ");
    printk_dec(pd_idx);
    printks(", PT index: ");
    printk_dec(pt_idx);
    printks(", Offset: 0x");
    printk_hex32(offset);
    printks("\n");

    // Read PML4 entry
    uptr pml4_base = 0x100000;  // Known PML4 location
    uptr pml4_entry_addr = pml4_base + (pml4_idx * 8);
    u64 pml4_entry = *(u64*)pml4_entry_addr;

    printks("  PML4[");
    printk_dec(pml4_idx);
    printks("] @ 0x");
    printk_hex64(pml4_entry_addr);
    printks(" = 0x");
    printk_hex64(pml4_entry);
    printks("\n");

    if (!(pml4_entry & 1)) {
        printks("  -> Not present\n");
        return;
    }

    // Continue with PDPT, PD, PT if present...
    printks("  (Full page table walk not implemented yet)\n");
}

// Dump x86 page tables
void platform_mem_dump_pagetables(void) {
    printks("\n[MEM] x86 Page Table Dump:\n");

    // PML4 at 0x100000
    printks("  PML4 (0x100000):\n");
    kmem_dump((const void*)0x100000, 64);

    // PDPT at 0x101000
    printks("\n  PDPT (0x101000):\n");
    kmem_dump((const void*)0x101000, 64);

    // PD0 at 0x102000 (first few entries)
    printks("\n  PD0 (0x102000) - First 4 entries:\n");
    kmem_dump((const void*)0x102000, 32);

    // PD3 at 0x103000 (MMIO region, first few entries)
    printks("\n  PD3 (0x103000) - First 4 entries:\n");
    kmem_dump((const void*)0x103000, 32);

    // PT at 0x104000 (kernel region, first few entries)
    printks("\n  PT (0x104000) - First 8 entries:\n");
    kmem_dump((const void*)0x104000, 64);
}

// Set memory guard (canary value)
void platform_mem_set_guard(void* addr, u32 size) {
    // For simplicity, write a pattern at the start and end
    if (size >= 8) {
        *(u64*)addr = 0xDEADBEEFCAFEBABEULL;
        if (size >= 16) {
            *(u64*)((uptr)addr + size - 8) = 0xDEADBEEFCAFEBABEULL;
        }
    }
}

// Check memory guard is intact
bool platform_mem_check_guard(void* addr, u32 size) {
    bool ok = true;

    if (size >= 8) {
        u64 val = *(u64*)addr;
        if (val != 0xDEADBEEFCAFEBABEULL) {
            printks("[MEM] GUARD FAILED at 0x");
            printk_hex64((uptr)addr);
            printks(": expected 0xDEADBEEFCAFEBABE, got 0x");
            printk_hex64(val);
            printks("\n");
            ok = false;
        }

        if (size >= 16) {
            val = *(u64*)((uptr)addr + size - 8);
            if (val != 0xDEADBEEFCAFEBABEULL) {
                printks("[MEM] GUARD FAILED at 0x");
                printk_hex64((uptr)addr + size - 8);
                printks(": expected 0xDEADBEEFCAFEBABE, got 0x");
                printk_hex64(val);
                printks("\n");
                ok = false;
            }
        }
    }

    return ok;
}

#endif // KDEBUG

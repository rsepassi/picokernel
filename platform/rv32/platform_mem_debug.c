// RISC-V 32 Platform Memory Debugging Implementation
// Implements kernel/mem_debug.h interface for RV32

#include "kernel/mem_debug.h"
#include "platform.h"
#include "kbase.h"

#ifdef KDEBUG

// RISC-V 32 memory layout (platform-specific)
// Note: RV32 uses SV32 paging (2-level)

// Validate critical memory regions
void platform_mem_validate_critical(void) {
    printks("\n[MEM] === RISC-V 32 Memory Region Validation ===\n");

    // TODO: Validate exception vectors (mtvec/stvec)
    // TODO: Validate kernel sections
    // TODO: Check stack guard regions

    printks("[MEM] RV32 memory validation not fully implemented yet\n");
    printks("[MEM] === Validation SKIPPED ===\n\n");
}

// Validate post-initialization state
void platform_mem_validate_post_init(platform_t *platform, void *fdt) {
    (void)platform;
    (void)fdt;
    printks("\n[MEM] === RV32 Post-Init Validation ===\n");
    printks("[MEM] RV32 post-init validation not implemented yet\n");
    printks("[MEM] === Validation SKIPPED ===\n\n");
}

// Print RV32 memory layout
void platform_mem_print_layout(void) {
    printks("\n[MEM] === RISC-V 32 Memory Map ===\n");
    printks("  Device Tree:         Passed in a1 register at boot\n");
    printks("  Kernel base:         0x80200000 (typical for QEMU virt)\n");
    printks("  RAM:                 0x80000000 - 0x88000000 (128 MiB)\n");
    printks("  MMIO (VirtIO):       0x10001000 - 0x10008000 (QEMU virt)\n");
    printks("  UART (NS16550):      0x10000000\n");
    printks("  PLIC (Interrupt):    0x0C000000 - 0x10000000\n");
    printks("  CLINT (Timer):       0x02000000 - 0x02010000\n");
    printks("\n");

    printks("Note: RV32 uses SV32 virtual memory (2-level paging)\n");
    printks("      satp register controls address translation\n\n");
}

// Dump virtual address translation (RISC-V SV32)
void platform_mem_dump_translation(uptr vaddr) {
    printks("\n[MEM] RV32 Virtual address translation for 0x");
    printk_hex64(vaddr);
    printks(":\n");

    // SV32: 2-level paging
    // satp → L1 → L0 → Physical
    // Each level uses 10 bits, 12-bit offset

    printks("  Address breakdown (SV32):\n");
    u32 vpn1 = (vaddr >> 22) & 0x3FF;  // VPN[1] - 10 bits
    u32 vpn0 = (vaddr >> 12) & 0x3FF;  // VPN[0] - 10 bits
    u32 offset = vaddr & 0xFFF;

    printks("  VPN[1]: ");
    printk_dec(vpn1);
    printks(", VPN[0]: ");
    printk_dec(vpn0);
    printks(", Offset: 0x");
    printk_hex32(offset);
    printks("\n");

    printks("  (Full page table walk requires satp register access)\n");
}

// Dump RV32 page tables
void platform_mem_dump_pagetables(void) {
    printks("\n[MEM] RV32 Page Table Dump:\n");
    printks("  RV32 page table dumping not implemented\n");
    printks("  (Requires reading satp CSR and walking page tables)\n\n");
}

// Set memory guard (canary value)
void platform_mem_set_guard(void* addr, u32 size) {
    // Generic implementation - write pattern at boundaries
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

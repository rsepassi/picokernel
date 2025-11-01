// RISC-V 64 Platform Memory Debugging Implementation
// Implements kernel/mem_debug.h interface for RV64

#include "kernel/mem_debug.h"
#include "platform.h"
#include "kbase.h"

#ifdef KDEBUG

// RISC-V 64 memory layout (platform-specific)
// Note: RV64 typically uses SV39 or SV48 paging

// Validate critical memory regions
void platform_mem_validate_critical(void) {
    printks("\n[MEM] === RISC-V 64 Memory Region Validation ===\n");

    // TODO: Validate exception vectors (mtvec/stvec)
    // TODO: Validate kernel sections
    // TODO: Check stack guard regions

    printks("[MEM] RV64 memory validation not fully implemented yet\n");
    printks("[MEM] === Validation SKIPPED ===\n\n");
}

// Print RV64 memory layout
void platform_mem_print_layout(void) {
    printks("\n[MEM] === RISC-V 64 Memory Map ===\n");
    printks("  Device Tree:         Passed in a1 register at boot\n");
    printks("  Kernel base:         0x80200000 (typical for QEMU virt)\n");
    printks("  RAM:                 0x80000000 - 0x88000000 (128 MiB default)\n");
    printks("  MMIO (VirtIO):       0x10001000 - 0x10008000 (QEMU virt)\n");
    printks("  UART (NS16550):      0x10000000\n");
    printks("  PLIC (Interrupt):    0x0C000000 - 0x10000000\n");
    printks("  CLINT (Timer):       0x02000000 - 0x02010000\n");
    printks("\n");

    printks("Note: RV64 uses SV39 or SV48 virtual memory\n");
    printks("      satp register controls address translation\n\n");
}

// Dump virtual address translation (RISC-V SV39/SV48)
void platform_mem_dump_translation(uptr vaddr) {
    printks("\n[MEM] RV64 Virtual address translation for 0x");
    printk_hex64(vaddr);
    printks(":\n");

    // SV39: 3-level paging (most common)
    // satp → L2 → L1 → L0 → Physical
    // Each level uses 9 bits, 12-bit offset

    printks("  Address breakdown (assuming SV39):\n");
    u32 vpn2 = (vaddr >> 30) & 0x1FF;  // VPN[2]
    u32 vpn1 = (vaddr >> 21) & 0x1FF;  // VPN[1]
    u32 vpn0 = (vaddr >> 12) & 0x1FF;  // VPN[0]
    u32 offset = vaddr & 0xFFF;

    printks("  VPN[2]: ");
    printk_dec(vpn2);
    printks(", VPN[1]: ");
    printk_dec(vpn1);
    printks(", VPN[0]: ");
    printk_dec(vpn0);
    printks(", Offset: 0x");
    printk_hex32(offset);
    printks("\n");

    printks("  (Full page table walk requires satp register access)\n");
}

// Dump RV64 page tables
void platform_mem_dump_pagetables(void) {
    printks("\n[MEM] RV64 Page Table Dump:\n");
    printks("  RV64 page table dumping not implemented\n");
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

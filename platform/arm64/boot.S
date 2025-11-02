// ARM64 Boot Assembly
// Minimal boot code to set up stack and jump to C
//
// NOTE: For bare-metal ELF kernels on QEMU ARM64 virt machine, the DTB location
// is NOT passed in a register. QEMU places the DTB at a fixed location (start of
// RAM, 0x40000000) when loading bare-metal ELF kernels. This is different from
// Linux kernels which use a special header that QEMU recognizes and passes DTB
// location in x0.
//
// This is technically a violation of the "NO HARDCODED ADDRESSES" principle, but
// it's unavoidable for bare-metal ARM64 ELF kernels on QEMU. On real hardware with
// a proper bootloader (U-Boot, EDK2, etc.), the DTB would be passed via x0.
//
// Our kernel is loaded at 0x40200000 (2MB into RAM)

.section .text.boot
.global _start

_start:
    // Set up stack pointer
    ldr x1, =stack_top
    mov sp, x1

    // Clear BSS section (zero-initialize static/global variables)
    ldr x1, =__bss_start
    ldr x2, =__bss_end
clear_bss_loop:
    cmp x1, x2
    b.hs clear_bss_done
    str xzr, [x1], #8
    b clear_bss_loop
clear_bss_done:

    // Pass device tree pointer to kmain in x0
    // QEMU virt machine places DTB at start of RAM (0x40000000) for ELF kernels
    // This is a known limitation of bare-metal ELF boot on QEMU ARM64
    ldr x0, =0x40000000

    // Branch to C entry point
    bl kmain

    // If kmain returns, hang
hang:
    wfe
    b hang

// Stack space
.section .bss
.align 16
.global stack_bottom
.global stack_top
stack_bottom:
    .skip 65536
stack_top:

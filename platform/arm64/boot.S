// ARM64 Boot Assembly
// Minimal boot code to set up stack and jump to C
//
// For ELF kernels, QEMU places the DTB at 0x40000000 (start of RAM)
// Our kernel is loaded at 0x40200000 (2MB into RAM)

.section .text.boot
.global _start

_start:
    // Set up stack pointer
    // Place stack near kernel at 0x40300000 (3 MB into RAM)
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
    // For ELF kernels, QEMU places DTB at start of RAM
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
stack_bottom:
    .skip 65536
stack_top:

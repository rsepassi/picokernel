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

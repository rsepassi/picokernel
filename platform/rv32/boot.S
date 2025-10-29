// RISC-V 32-bit Boot Assembly
// Minimal boot code to set up stack and jump to C

.section .text.boot
.global _start

_start:
    // OpenSBI will call us in S-mode with:
    // a0 = hartid (hardware thread ID)
    // a1 = pointer to device tree blob

    // Set up stack pointer
    la sp, stack_top

    // Pass DTB pointer as first argument to kmain
    mv a0, a1

    // Branch to C entry point
    call kmain

    // If main returns, hang
hang:
    wfi
    j hang

// Stack space (16 KiB)
.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

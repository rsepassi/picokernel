// RISC-V 64-bit Boot Assembly
// Minimal boot code to set up stack and jump to C

.section .text.boot
.global _start

_start:
    // Set up stack pointer
    la sp, stack_top

    // Branch to C entry point
    call main

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

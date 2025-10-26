// ARM64 Boot Assembly
// Minimal boot code to set up stack and jump to C

.section .text.boot
.global _start

_start:
    // Set up stack pointer
    // Place stack at 0x40100000 (1 MiB into RAM)
    ldr x0, =stack_top
    mov sp, x0

    // Branch to C entry point
    bl main

    // If main returns, hang
hang:
    wfe
    b hang

// Stack space (16 KiB)
.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

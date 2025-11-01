// RISC-V 64-bit Boot Assembly
// Minimal boot code to set up stack and jump to C

.section .text.boot
.global _start

_start:
    // Set up stack pointer
    la sp, stack_top

    // Clear BSS section (zero-initialize static/global variables)
    la t0, __bss_start
    la t1, __bss_end
clear_bss_loop:
    bgeu t0, t1, clear_bss_done
    sd zero, 0(t0)
    addi t0, t0, 8
    j clear_bss_loop
clear_bss_done:

    // RISC-V boot protocol:
    // a0 = hartid (hardware thread ID)
    // a1 = pointer to device tree blob
    // Pass DTB pointer as first argument to main
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
.global stack_bottom
.global stack_top
stack_bottom:
    .skip 16384
stack_top:

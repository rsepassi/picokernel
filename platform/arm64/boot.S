// ARM64 Boot Assembly
// Minimal boot code to set up stack and jump to C
//
// QEMU boot puts DTB at fixed location (start of RAM, 0x40000000)
//
// Our kernel is loaded at 0x40200000 (2MB into RAM)

.section .text.boot
.global _start

_start:
    // Disable all interrupts (D=Debug, A=SError, I=IRQ, F=FIQ)
    // This ensures we start in a known state regardless of bootloader behavior
    msr daifset, #0xF

    // Check that we're running at EL1 (Exception Level 1)
    // QEMU should boot us at EL1, but verify to catch misconfigurations
    mrs x1, CurrentEL
    lsr x1, x1, #2        // Extract EL bits [3:2]
    cmp x1, #1            // Check if EL1
    b.ne hang             // Branch if not EL1

    // Set up stack pointer
    ldr x1, =stack_top
    mov sp, x1

    // Clear BSS section (zero-initialize static/global variables)
    ldr x1, =_bss_start
    ldr x2, =_bss_end
clear_bss_loop:
    cmp x1, x2
    b.hs clear_bss_done
    str xzr, [x1], #8
    b clear_bss_loop
clear_bss_done:

    // Branch to C entry point
    ldr x0, =0x40000000
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

// ARM64 Boot Assembly
// Minimal boot code to set up stack and jump to C
//
// Boot Protocol: For bare-metal ELF kernels on QEMU ARM64, the bootloader behavior
// depends on the kernel format:
//   - Linux kernel (with kernel header): DTB passed in x0
//   - Bare-metal ELF (our case): DTB at fixed location (start of RAM, 0x40000000)
//
// We preserve x0 anyway and try to use it first. If it's valid, use it. If not,
// fall back to the known DTB location. The C code will validate the FDT header.
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
    b.ne wrong_el         // Branch if not EL1

    // x0 may contain FDT pointer from bootloader - preserve it just in case
    // Save to callee-saved register x19 before modifying other registers
    mov x19, x0

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

    // Try to use x0 if it looks valid (non-zero and reasonable)
    // Otherwise fall back to known DTB location for QEMU virt
    cmp x19, #0
    beq use_fixed_dtb
    ldr x1, =0x50000000  // Upper bound check
    cmp x19, x1
    bhs use_fixed_dtb

    // x19 looks reasonable, use it
    mov x0, x19
    b call_kmain

use_fixed_dtb:
    // Fall back to QEMU virt's known DTB location
    ldr x0, =0x40000000

call_kmain:
    // Branch to C entry point
    bl kmain

    // If kmain returns, hang
hang:
    wfe
    b hang

// Error handler: wrong exception level
wrong_el:
    // Infinite loop - no way to print error without UART initialization
    wfe
    b wrong_el

// Stack space
.section .bss
.align 16
.global stack_bottom
.global stack_top
stack_bottom:
    .skip 65536
stack_top:

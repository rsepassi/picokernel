// RISC-V 64-bit Boot Assembly
// Minimal boot code to set up stack and jump to C

.section .text.boot
.global _start

_start:
    // Verify we are in S-mode (Supervisor mode)
    // Set up minimal trap handler first - if we're in S-mode and try to
    // access M-mode CSRs, we'll trap here and continue booting
    la t0, _mode_check_trap
    csrw stvec, t0

    // Try to read mstatus (M-mode only CSR)
    // If in S-mode: will trap to _mode_check_trap -> continue boot (good!)
    // If in M-mode: will succeed -> hang (bad - we require S-mode)
    csrr t0, mstatus

    // If we reach here, we're in M-mode (not S-mode) - hang with wfi
_mode_check_hang:
    wfi
    j _mode_check_hang

_mode_check_trap:
    // Trapped trying to access M-mode CSR - we're in S-mode (good!)
    // Continue normal boot process
_boot_continue:
    // Disable interrupts explicitly (clear SIE bit in sstatus)
    csrci sstatus, 0x2

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

// Stack space (64 KiB)
.section .bss
.align 16
.global stack_bottom
.global stack_top
stack_bottom:
    .skip 65536
stack_top:

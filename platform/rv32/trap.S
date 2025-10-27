// RISC-V Trap Handler
// Assembly code to save/restore context and call C trap handler

.section .text
.align 4
.global trap_entry

// Trap entry point
// This is called when any interrupt or exception occurs
trap_entry:
    // Save all registers to the stack
    // We need to save all caller-saved and callee-saved registers
    // because we don't know what the C handler will use

    // Allocate space on stack for context (32 registers * 4 bytes = 128 bytes)
    addi sp, sp, -128

    // Save all general-purpose registers
    sw x1,  0(sp)   // ra
    sw x2,  4(sp)   // sp (save original sp value)
    sw x3,  8(sp)   // gp
    sw x4,  12(sp)  // tp
    sw x5,  16(sp)  // t0
    sw x6,  20(sp)  // t1
    sw x7,  24(sp)  // t2
    sw x8,  28(sp)  // s0/fp
    sw x9,  32(sp)  // s1
    sw x10, 36(sp)  // a0
    sw x11, 40(sp)  // a1
    sw x12, 44(sp)  // a2
    sw x13, 48(sp)  // a3
    sw x14, 52(sp)  // a4
    sw x15, 56(sp)  // a5
    sw x16, 60(sp)  // a6
    sw x17, 64(sp)  // a7
    sw x18, 68(sp)  // s2
    sw x19, 72(sp)  // s3
    sw x20, 76(sp)  // s4
    sw x21, 80(sp)  // s5
    sw x22, 84(sp)  // s6
    sw x23, 88(sp)  // s7
    sw x24, 92(sp)  // s8
    sw x25, 96(sp)  // s9
    sw x26, 100(sp) // s10
    sw x27, 104(sp) // s11
    sw x28, 108(sp) // t3
    sw x29, 112(sp) // t4
    sw x30, 116(sp) // t5
    sw x31, 120(sp) // t6

    // Fix sp saved value - it should be the value before we allocated space
    addi t0, sp, 128
    sw t0, 4(sp)

    // Call C trap handler
    call trap_handler

    // Restore all registers
    lw x1,  0(sp)   // ra
    // Skip x2 (sp) - restore it last
    lw x3,  8(sp)   // gp
    lw x4,  12(sp)  // tp
    lw x5,  16(sp)  // t0
    lw x6,  20(sp)  // t1
    lw x7,  24(sp)  // t2
    lw x8,  28(sp)  // s0/fp
    lw x9,  32(sp)  // s1
    lw x10, 36(sp)  // a0
    lw x11, 40(sp)  // a1
    lw x12, 44(sp)  // a2
    lw x13, 48(sp)  // a3
    lw x14, 52(sp)  // a4
    lw x15, 56(sp)  // a5
    lw x16, 60(sp)  // a6
    lw x17, 64(sp)  // a7
    lw x18, 68(sp)  // s2
    lw x19, 72(sp)  // s3
    lw x20, 76(sp)  // s4
    lw x21, 80(sp)  // s5
    lw x22, 84(sp)  // s6
    lw x23, 88(sp)  // s7
    lw x24, 92(sp)  // s8
    lw x25, 96(sp)  // s9
    lw x26, 100(sp) // s10
    lw x27, 104(sp) // s11
    lw x28, 108(sp) // t3
    lw x29, 112(sp) // t4
    lw x30, 116(sp) // t5
    lw x31, 120(sp) // t6

    // Restore stack pointer
    addi sp, sp, 128

    // Return from trap (supervisor mode)
    sret

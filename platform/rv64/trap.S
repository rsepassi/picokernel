// RISC-V Trap Handler
// Save context and call C trap handler

.section .text
.align 4

// Trap vector - called when any trap (interrupt or exception) occurs
.globl trap_vector
.type trap_vector, @function
trap_vector:
    // On entry, CPU has already:
    // - Set sepc to the faulting/interrupted PC
    // - Set scause to trap cause
    // - Set stval to additional trap information
    // - Disabled interrupts (cleared SIE bit in sstatus)

    // We need to save ALL registers before calling C code
    // RISC-V calling convention: we must preserve all callee-saved regs

    // Create stack frame for saved registers
    // We need space for 31 registers (x1-x31, x0 is always zero)
    // Plus we'll save sepc, sstatus, scause, stval
    // Total: 35 * 8 = 280 bytes, round to 288 for alignment

    addi sp, sp, -288

    // Save all general-purpose registers (except x0/zero and sp for now)
    sd x1,  0(sp)   // ra
    sd x3,  16(sp)  // gp
    sd x4,  24(sp)  // tp
    sd x5,  32(sp)  // t0
    sd x6,  40(sp)  // t1
    sd x7,  48(sp)  // t2
    sd x8,  56(sp)  // s0/fp
    sd x9,  64(sp)  // s1
    sd x10, 72(sp)  // a0
    sd x11, 80(sp)  // a1
    sd x12, 88(sp)  // a2
    sd x13, 96(sp)  // a3
    sd x14, 104(sp) // a4
    sd x15, 112(sp) // a5
    sd x16, 120(sp) // a6
    sd x17, 128(sp) // a7
    sd x18, 136(sp) // s2
    sd x19, 144(sp) // s3
    sd x20, 152(sp) // s4
    sd x21, 160(sp) // s5
    sd x22, 168(sp) // s6
    sd x23, 176(sp) // s7
    sd x24, 184(sp) // s8
    sd x25, 192(sp) // s9
    sd x26, 200(sp) // s10
    sd x27, 208(sp) // s11
    sd x28, 216(sp) // t3
    sd x29, 224(sp) // t4
    sd x30, 232(sp) // t5
    sd x31, 240(sp) // t6

    // Save original sp (before we adjusted it)
    addi t0, sp, 288
    sd t0, 8(sp)    // sp (x2)

    // Save CSRs
    csrr t0, sepc
    sd t0, 248(sp)

    csrr t0, sstatus
    sd t0, 256(sp)

    csrr t0, scause
    sd t0, 264(sp)

    csrr t0, stval
    sd t0, 272(sp)

    // Call C trap handler: void trap_handler(uint64_t scause, uint64_t sepc, uint64_t stval)
    ld a0, 264(sp)  // scause
    ld a1, 248(sp)  // sepc
    ld a2, 272(sp)  // stval
    call trap_handler

    // Restore CSRs
    ld t0, 248(sp)
    csrw sepc, t0

    ld t0, 256(sp)
    csrw sstatus, t0

    // Restore all general-purpose registers
    ld x1,  0(sp)   // ra
    ld x3,  16(sp)  // gp
    ld x4,  24(sp)  // tp
    ld x5,  32(sp)  // t0
    ld x6,  40(sp)  // t1
    ld x7,  48(sp)  // t2
    ld x8,  56(sp)  // s0/fp
    ld x9,  64(sp)  // s1
    ld x10, 72(sp)  // a0
    ld x11, 80(sp)  // a1
    ld x12, 88(sp)  // a2
    ld x13, 96(sp)  // a3
    ld x14, 104(sp) // a4
    ld x15, 112(sp) // a5
    ld x16, 120(sp) // a6
    ld x17, 128(sp) // a7
    ld x18, 136(sp) // s2
    ld x19, 144(sp) // s3
    ld x20, 152(sp) // s4
    ld x21, 160(sp) // s5
    ld x22, 168(sp) // s6
    ld x23, 176(sp) // s7
    ld x24, 184(sp) // s8
    ld x25, 192(sp) // s9
    ld x26, 200(sp) // s10
    ld x27, 208(sp) // s11
    ld x28, 216(sp) // t3
    ld x29, 224(sp) // t4
    ld x30, 232(sp) // t5
    ld x31, 240(sp) // t6

    // Restore sp
    ld sp, 8(sp)

    // Return from trap
    sret

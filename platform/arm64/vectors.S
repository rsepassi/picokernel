// ARM64 Exception Vector Table
// Exception handlers that save context and call C handlers

.section .text

// Exception vector table
// Each entry is 128 bytes (0x80), 16 entries total (4 exception levels x 4 types)
// Reference: ARM Architecture Reference Manual, Exception Vector Table
.balign 0x800
.global exception_vector_table
exception_vector_table:

// Current EL with SP0
.balign 0x80
curr_el_sp0_sync:
    b exception_entry_sync_sp0

.balign 0x80
curr_el_sp0_irq:
    b exception_entry_irq_sp0

.balign 0x80
curr_el_sp0_fiq:
    b exception_entry_fiq_sp0

.balign 0x80
curr_el_sp0_serror:
    b exception_entry_serror_sp0

// Current EL with SPx
.balign 0x80
curr_el_spx_sync:
    b exception_entry_sync_spx

.balign 0x80
curr_el_spx_irq:
    b exception_entry_irq_spx

.balign 0x80
curr_el_spx_fiq:
    b exception_entry_fiq_spx

.balign 0x80
curr_el_spx_serror:
    b exception_entry_serror_spx

// Lower EL using AArch64
.balign 0x80
lower_el_aarch64_sync:
    b exception_entry_sync_lower64

.balign 0x80
lower_el_aarch64_irq:
    b exception_entry_irq_lower64

.balign 0x80
lower_el_aarch64_fiq:
    b exception_entry_fiq_lower64

.balign 0x80
lower_el_aarch64_serror:
    b exception_entry_serror_lower64

// Lower EL using AArch32
.balign 0x80
lower_el_aarch32_sync:
    b exception_entry_sync_lower32

.balign 0x80
lower_el_aarch32_irq:
    b exception_entry_irq_lower32

.balign 0x80
lower_el_aarch32_fiq:
    b exception_entry_fiq_lower32

.balign 0x80
lower_el_aarch32_serror:
    b exception_entry_serror_lower32

// Macro to save all general-purpose registers
.macro save_regs
    // Make room on stack for context (264 bytes = 33 x 8-byte registers)
    sub sp, sp, #264

    // Save general-purpose registers x0-x30
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]

    // Save ELR_EL1 (exception return address)
    mrs x0, elr_el1
    str x0, [sp, #248]

    // Save SPSR_EL1 (saved program status)
    mrs x0, spsr_el1
    str x0, [sp, #256]
.endm

// Macro to restore all general-purpose registers
.macro restore_regs
    // Restore SPSR_EL1
    ldr x0, [sp, #256]
    msr spsr_el1, x0

    // Restore ELR_EL1
    ldr x0, [sp, #248]
    msr elr_el1, x0

    // Restore general-purpose registers
    ldr x30, [sp, #240]
    ldp x28, x29, [sp, #224]
    ldp x26, x27, [sp, #208]
    ldp x24, x25, [sp, #192]
    ldp x22, x23, [sp, #176]
    ldp x20, x21, [sp, #160]
    ldp x18, x19, [sp, #144]
    ldp x16, x17, [sp, #128]
    ldp x14, x15, [sp, #112]
    ldp x12, x13, [sp, #96]
    ldp x10, x11, [sp, #80]
    ldp x8, x9, [sp, #64]
    ldp x6, x7, [sp, #48]
    ldp x4, x5, [sp, #32]
    ldp x2, x3, [sp, #16]
    ldp x0, x1, [sp, #0]

    // Restore stack pointer
    add sp, sp, #264
.endm

// Exception entry points
exception_entry_sync_sp0:
    save_regs
    mov x0, #0  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_irq_sp0:
    save_regs
    mov x0, #1  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_fiq_sp0:
    save_regs
    mov x0, #2  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_serror_sp0:
    save_regs
    mov x0, #3  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_sync_spx:
    save_regs
    mov x0, #4  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_irq_spx:
    save_regs
    mov x0, #5  // Exception type (IRQ from current EL)
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_fiq_spx:
    save_regs
    mov x0, #6  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_serror_spx:
    save_regs
    mov x0, #7  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_sync_lower64:
    save_regs
    mov x0, #8  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_irq_lower64:
    save_regs
    mov x0, #9  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_fiq_lower64:
    save_regs
    mov x0, #10  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_serror_lower64:
    save_regs
    mov x0, #11  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_sync_lower32:
    save_regs
    mov x0, #12  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_irq_lower32:
    save_regs
    mov x0, #13  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_fiq_lower32:
    save_regs
    mov x0, #14  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

exception_entry_serror_lower32:
    save_regs
    mov x0, #15  // Exception type
    mrs x1, esr_el1
    mrs x2, elr_el1
    mrs x3, far_el1
    bl exception_handler
    restore_regs
    eret

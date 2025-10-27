// ARM32 Exception Vector Table
// Exception handlers that save context and call C handlers

.section .text.vectors
.align 5

// Exception vector table (must be 32-byte aligned)
// ARM requires the vector table at a specific location
.global vectors_start
vectors_start:
    ldr pc, reset_handler_addr
    ldr pc, undefined_handler_addr
    ldr pc, svc_handler_addr
    ldr pc, prefetch_abort_handler_addr
    ldr pc, data_abort_handler_addr
    ldr pc, reserved_handler_addr
    ldr pc, irq_handler_addr
    ldr pc, fiq_handler_addr

// Handler addresses (literal pool)
reset_handler_addr:          .word reset_handler
undefined_handler_addr:      .word undefined_handler
svc_handler_addr:            .word svc_handler
prefetch_abort_handler_addr: .word prefetch_abort_handler
data_abort_handler_addr:     .word data_abort_handler
reserved_handler_addr:       .word reserved_handler
irq_handler_addr:            .word irq_handler
fiq_handler_addr:            .word fiq_handler

.section .text

// Reset handler (should not be called in normal operation)
reset_handler:
    b .  // Infinite loop

// Undefined instruction handler
undefined_handler:
    // Save context and call C handler with vector number 1
    sub lr, lr, #4        // Adjust return address
    push {r0-r12, lr}     // Save registers
    mov r0, #1            // Vector number
    bl interrupt_handler
    pop {r0-r12, pc}      // Restore and return

// SVC (Software interrupt / Supervisor Call) handler
svc_handler:
    // Save context and call C handler with vector number 2
    push {r0-r12, lr}     // Save registers
    mov r0, #2            // Vector number
    bl interrupt_handler
    pop {r0-r12, pc}      // Restore and return

// Prefetch abort handler
prefetch_abort_handler:
    // Save context and call C handler with vector number 3
    sub lr, lr, #4        // Adjust return address
    push {r0-r12, lr}     // Save registers
    mov r0, #3            // Vector number
    bl interrupt_handler
    pop {r0-r12, pc}      // Restore and return

// Data abort handler
data_abort_handler:
    // Save context and call C handler with vector number 4
    sub lr, lr, #8        // Adjust return address
    push {r0-r12, lr}     // Save registers
    mov r0, #4            // Vector number
    bl interrupt_handler
    pop {r0-r12, pc}      // Restore and return

// Reserved handler
reserved_handler:
    // Save context and call C handler with vector number 5
    push {r0-r12, lr}     // Save registers
    mov r0, #5            // Vector number
    bl interrupt_handler
    pop {r0-r12, pc}      // Restore and return

// IRQ (Interrupt Request) handler
irq_handler:
    // Save context and call C handler with vector number 6
    sub lr, lr, #4        // Adjust return address
    push {r0-r12, lr}     // Save registers
    mov r0, #6            // Vector number
    bl interrupt_handler
    pop {r0-r12, lr}      // Restore registers
    subs pc, lr, #0       // Return from interrupt

// FIQ (Fast Interrupt Request) handler
fiq_handler:
    // Save context and call C handler with vector number 7
    sub lr, lr, #4        // Adjust return address
    push {r0-r12, lr}     // Save registers
    mov r0, #7            // Vector number
    bl interrupt_handler
    pop {r0-r12, lr}      // Restore registers
    subs pc, lr, #0       // Return from interrupt

// Function to set VBAR (Vector Base Address Register)
.global set_vbar
set_vbar:
    mcr p15, 0, r0, c12, c0, 0  // Write VBAR
    bx lr

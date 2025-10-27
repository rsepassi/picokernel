// x64 Interrupt Service Routine stubs
// Assembly handlers that save context and call C handlers

.section .text

// Macro to create exception stubs WITHOUT error code
// These exceptions need a dummy error code pushed for stack alignment
.macro ISR_NOERR num
.global isr_stub_\num
.type isr_stub_\num, @function
isr_stub_\num:
    push $0                 // Dummy error code
    push $\num              // Push vector number
    jmp isr_common_handler
.endm

// Macro to create exception stubs WITH error code
// CPU already pushed error code, we just push vector number
.macro ISR_ERR num
.global isr_stub_\num
.type isr_stub_\num, @function
isr_stub_\num:
    push $\num              // Push vector number (error code already on stack)
    jmp isr_common_handler
.endm

// Generate stubs for all 32 exception vectors
ISR_NOERR 0     // Divide Error
ISR_NOERR 1     // Debug
ISR_NOERR 2     // NMI
ISR_NOERR 3     // Breakpoint
ISR_NOERR 4     // Overflow
ISR_NOERR 5     // Bound Range Exceeded
ISR_NOERR 6     // Invalid Opcode
ISR_NOERR 7     // Device Not Available
ISR_ERR   8     // Double Fault (with error code)
ISR_NOERR 9     // Coprocessor Segment Overrun (legacy)
ISR_ERR   10    // Invalid TSS (with error code)
ISR_ERR   11    // Segment Not Present (with error code)
ISR_ERR   12    // Stack Fault (with error code)
ISR_ERR   13    // General Protection (with error code)
ISR_ERR   14    // Page Fault (with error code)
ISR_NOERR 15    // Reserved
ISR_NOERR 16    // x87 FPU Error
ISR_ERR   17    // Alignment Check (with error code)
ISR_NOERR 18    // Machine Check
ISR_NOERR 19    // SIMD Floating-Point Exception
ISR_NOERR 20    // Virtualization Exception
ISR_ERR   21    // Control Protection Exception (with error code)
ISR_NOERR 22    // Reserved
ISR_NOERR 23    // Reserved
ISR_NOERR 24    // Reserved
ISR_NOERR 25    // Reserved
ISR_NOERR 26    // Reserved
ISR_NOERR 27    // Reserved
ISR_NOERR 28    // Hypervisor Injection Exception
ISR_NOERR 29    // VMM Communication Exception
ISR_ERR   30    // Security Exception (with error code)
ISR_NOERR 31    // Reserved

// Common exception handler
// Stack layout on entry: [error_code, vector_num, RIP, CS, RFLAGS, ...]
isr_common_handler:
    // Save ALL general-purpose registers
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    // At this point stack has:
    // [regs, error_code, vector_num, RIP, CS, RFLAGS, ...]
    // Vector number is at offset 15*8 = 120 bytes from RSP

    // Load vector number into RDI (first argument)
    mov 120(%rsp), %rdi

    // Call C interrupt handler
    call interrupt_handler

    // Restore all registers
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax

    // Pop error code and vector number
    add $16, %rsp

    // Return from interrupt
    iretq

// Timer interrupt stub (vector 32)
.global isr_stub_32
.type isr_stub_32, @function
isr_stub_32:
    // Save ALL general-purpose registers (since interrupt can happen anywhere)
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    // CPU pushes: In 64-bit mode without privilege change:
    //   - RIP, CS, RFLAGS (24 bytes)
    // We pushed 120 bytes (15 registers)
    // Total: 144 bytes = 16-byte aligned
    // CALL adds 8 bytes, making RSP 8-byte aligned in function (correct for ABI)

    // Call C interrupt handler with vector number 32
    mov $32, %rdi           // First argument: vector number
    call interrupt_handler

    // Restore all registers
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax

    // Return from interrupt
    iretq

// Spurious interrupt stub (vector 255)
// Minimal handler - spurious interrupts don't need EOI or processing
.global isr_stub_255
.type isr_stub_255, @function
isr_stub_255:
    // Just return immediately - no EOI, no handler call needed
    iretq

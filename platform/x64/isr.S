// x64 Interrupt Service Routine stubs
// Assembly handlers that save context and call C handlers

.section .text

// Common ISR stub for exceptions and unhandled interrupts
.global isr_stub_common
.type isr_stub_common, @function
isr_stub_common:
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

    // Call C interrupt handler with vector number 0 (will be wrong, but that's OK for now)
    mov $0, %rdi
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
.global isr_stub_255
.type isr_stub_255, @function
isr_stub_255:
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

    // Call C interrupt handler with vector number 255
    mov $255, %rdi
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

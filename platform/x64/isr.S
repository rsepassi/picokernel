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
    // [regs(120 bytes), error_code(8), vector_num(8), RIP, CS, RFLAGS, ...]
    // Vector number is at offset 15*8 + 8 = 128 bytes from RSP
    // Need to align stack to 16 bytes before call (x86-64 ABI requirement)

    // Align stack to 16 bytes
    sub $8, %rsp

    // Load vector number into RDI (first argument)
    // After sub $8, offset is now 128 + 8 = 136
    mov 136(%rsp), %rdi

    // Call C interrupt handler
    call interrupt_handler

    // Remove alignment padding
    add $8, %rsp

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

    // Align stack to 16 bytes for x86-64 ABI
    // CPU pushed 24 bytes (RIP+CS+RFLAGS), we pushed 120 bytes (15 regs) = 144 total
    // Need to push 8 more bytes so RSP is 16-byte aligned before CALL
    sub $8, %rsp

    // Call C interrupt handler with vector number 32
    mov $32, %rdi           // First argument: vector number
    call interrupt_handler

    // Remove alignment padding
    add $8, %rsp

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

// IRQ interrupt stubs (vectors 33-47)
// Macro to create IRQ stubs
.macro ISR_IRQ num
.global isr_stub_\num
.type isr_stub_\num, @function
isr_stub_\num:
    // Save all registers
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

    // Align stack to 16 bytes for x86-64 ABI
    // CPU pushed 24 bytes (RIP+CS+RFLAGS), we pushed 120 bytes (15 regs) = 144 total
    // Need to push 8 more bytes so RSP is 16-byte aligned before CALL
    sub $8, %rsp

    mov $\num, %rdi
    call interrupt_handler

    // Remove alignment padding
    add $8, %rsp

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

    iretq
.endm

ISR_IRQ 33
ISR_IRQ 34
ISR_IRQ 35
ISR_IRQ 36
ISR_IRQ 37
ISR_IRQ 38
ISR_IRQ 39
ISR_IRQ 40
ISR_IRQ 41
ISR_IRQ 42
ISR_IRQ 43
ISR_IRQ 44
ISR_IRQ 45
ISR_IRQ 46
ISR_IRQ 47
ISR_IRQ 48
ISR_IRQ 49
ISR_IRQ 50
ISR_IRQ 51
ISR_IRQ 52
ISR_IRQ 53
ISR_IRQ 54
ISR_IRQ 55
ISR_IRQ 56
ISR_IRQ 57
ISR_IRQ 58
ISR_IRQ 59
ISR_IRQ 60
ISR_IRQ 61
ISR_IRQ 62
ISR_IRQ 63
ISR_IRQ 64
ISR_IRQ 65
ISR_IRQ 66
ISR_IRQ 67
ISR_IRQ 68
ISR_IRQ 69
ISR_IRQ 70
ISR_IRQ 71
ISR_IRQ 72
ISR_IRQ 73
ISR_IRQ 74
ISR_IRQ 75
ISR_IRQ 76
ISR_IRQ 77
ISR_IRQ 78
ISR_IRQ 79

// Additional IRQ stubs for high vectors (80-254)
// These are used for MSI-X vectors (128+) and additional IOAPIC routing
ISR_IRQ 80
ISR_IRQ 81
ISR_IRQ 82
ISR_IRQ 83
ISR_IRQ 84
ISR_IRQ 85
ISR_IRQ 86
ISR_IRQ 87
ISR_IRQ 88
ISR_IRQ 89
ISR_IRQ 90
ISR_IRQ 91
ISR_IRQ 92
ISR_IRQ 93
ISR_IRQ 94
ISR_IRQ 95
ISR_IRQ 96
ISR_IRQ 97
ISR_IRQ 98
ISR_IRQ 99
ISR_IRQ 100
ISR_IRQ 101
ISR_IRQ 102
ISR_IRQ 103
ISR_IRQ 104
ISR_IRQ 105
ISR_IRQ 106
ISR_IRQ 107
ISR_IRQ 108
ISR_IRQ 109
ISR_IRQ 110
ISR_IRQ 111
ISR_IRQ 112
ISR_IRQ 113
ISR_IRQ 114
ISR_IRQ 115
ISR_IRQ 116
ISR_IRQ 117
ISR_IRQ 118
ISR_IRQ 119
ISR_IRQ 120
ISR_IRQ 121
ISR_IRQ 122
ISR_IRQ 123
ISR_IRQ 124
ISR_IRQ 125
ISR_IRQ 126
ISR_IRQ 127
ISR_IRQ 128
ISR_IRQ 129
ISR_IRQ 130
ISR_IRQ 131
ISR_IRQ 132
ISR_IRQ 133
ISR_IRQ 134
ISR_IRQ 135
ISR_IRQ 136
ISR_IRQ 137
ISR_IRQ 138
ISR_IRQ 139
ISR_IRQ 140
ISR_IRQ 141
ISR_IRQ 142
ISR_IRQ 143
ISR_IRQ 144
ISR_IRQ 145
ISR_IRQ 146
ISR_IRQ 147
ISR_IRQ 148
ISR_IRQ 149
ISR_IRQ 150
ISR_IRQ 151
ISR_IRQ 152
ISR_IRQ 153
ISR_IRQ 154
ISR_IRQ 155
ISR_IRQ 156
ISR_IRQ 157
ISR_IRQ 158
ISR_IRQ 159
ISR_IRQ 160
ISR_IRQ 161
ISR_IRQ 162
ISR_IRQ 163
ISR_IRQ 164
ISR_IRQ 165
ISR_IRQ 166
ISR_IRQ 167
ISR_IRQ 168
ISR_IRQ 169
ISR_IRQ 170
ISR_IRQ 171
ISR_IRQ 172
ISR_IRQ 173
ISR_IRQ 174
ISR_IRQ 175
ISR_IRQ 176
ISR_IRQ 177
ISR_IRQ 178
ISR_IRQ 179
ISR_IRQ 180
ISR_IRQ 181
ISR_IRQ 182
ISR_IRQ 183
ISR_IRQ 184
ISR_IRQ 185
ISR_IRQ 186
ISR_IRQ 187
ISR_IRQ 188
ISR_IRQ 189
ISR_IRQ 190
ISR_IRQ 191
ISR_IRQ 192
ISR_IRQ 193
ISR_IRQ 194
ISR_IRQ 195
ISR_IRQ 196
ISR_IRQ 197
ISR_IRQ 198
ISR_IRQ 199
ISR_IRQ 200
ISR_IRQ 201
ISR_IRQ 202
ISR_IRQ 203
ISR_IRQ 204
ISR_IRQ 205
ISR_IRQ 206
ISR_IRQ 207
ISR_IRQ 208
ISR_IRQ 209
ISR_IRQ 210
ISR_IRQ 211
ISR_IRQ 212
ISR_IRQ 213
ISR_IRQ 214
ISR_IRQ 215
ISR_IRQ 216
ISR_IRQ 217
ISR_IRQ 218
ISR_IRQ 219
ISR_IRQ 220
ISR_IRQ 221
ISR_IRQ 222
ISR_IRQ 223
ISR_IRQ 224
ISR_IRQ 225
ISR_IRQ 226
ISR_IRQ 227
ISR_IRQ 228
ISR_IRQ 229
ISR_IRQ 230
ISR_IRQ 231
ISR_IRQ 232
ISR_IRQ 233
ISR_IRQ 234
ISR_IRQ 235
ISR_IRQ 236
ISR_IRQ 237
ISR_IRQ 238
ISR_IRQ 239
ISR_IRQ 240
ISR_IRQ 241
ISR_IRQ 242
ISR_IRQ 243
ISR_IRQ 244
ISR_IRQ 245
ISR_IRQ 246
ISR_IRQ 247
ISR_IRQ 248
ISR_IRQ 249
ISR_IRQ 250
ISR_IRQ 251
ISR_IRQ 252
ISR_IRQ 253
ISR_IRQ 254

// Spurious interrupt stub (vector 255)
// Minimal handler - spurious interrupts don't need EOI or processing
.global isr_stub_255
.type isr_stub_255, @function
isr_stub_255:
    // Just return immediately - no EOI, no handler call needed
    iretq

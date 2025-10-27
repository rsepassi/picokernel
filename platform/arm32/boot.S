// ARM32 Boot Assembly
// Minimal boot code to set up stack and jump to C
//
// For ELF kernels, QEMU places the DTB at 0x40000000 (start of RAM)
// Our kernel is loaded at 0x40200000 (2MB into RAM)

.section .text.boot
.global _start

_start:
    // Set up Supervisor mode stack
    // Place stack near kernel at 0x40300000 (3 MB into RAM)
    ldr r1, =svc_stack_top
    mov sp, r1

    // Set up IRQ mode stack (required for interrupt handling)
    // IRQ mode has its own stack pointer (SP_irq)
    mrs r0, cpsr              // Read current program status
    bic r0, r0, #0x1F         // Clear mode bits
    orr r0, r0, #0x12         // Set IRQ mode (0x12)
    msr cpsr_c, r0            // Switch to IRQ mode
    ldr sp, =irq_stack_top    // Set IRQ mode stack pointer

    // Switch back to Supervisor mode
    mrs r0, cpsr
    bic r0, r0, #0x1F         // Clear mode bits
    orr r0, r0, #0x13         // Set Supervisor mode (0x13)
    msr cpsr_c, r0            // Back to Supervisor mode

    // Pass device tree pointer to main in r0
    // For ELF kernels, QEMU places DTB at start of RAM
    ldr r0, =0x40000000

    // Branch to C entry point
    bl main

    // If main returns, hang
hang:
    wfe
    b hang

// Stack space
.section .bss
.align 16

// Supervisor mode stack (16 KiB)
svc_stack_bottom:
    .skip 16384
svc_stack_top:

// IRQ mode stack (8 KiB)
irq_stack_bottom:
    .skip 8192
irq_stack_top:

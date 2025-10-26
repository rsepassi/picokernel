// x64 Boot Assembly
// Minimal boot code to set up stack and jump to C

// PVH boot note for QEMU direct kernel loading
.section .note.Xen, "a"
.align 4
.long 4                     // namesz
.long 12                    // descsz
.long 18                    // type = XEN_ELFNOTE_PHYS32_ENTRY
.asciz "Xen"
.align 4
.long _start                // entry point (physical address)
.long 0                     // reserved
.long 0                     // reserved

.section .text.boot
.global _start
.code64

_start:
    // Clear interrupts
    cli

    // Set up stack pointer
    lea stack_top(%rip), %rsp

    // Clear direction flag
    cld

    // Branch to C entry point
    call main

    // If main returns, hang
hang:
    hlt
    jmp hang

// Stack space (16 KiB)
.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

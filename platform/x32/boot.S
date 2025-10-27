// x32 Boot Assembly - PVH boot entry (32-bit mode)
// x32 ABI uses 64-bit long mode with 32-bit pointers (ILP32)

// PVH note
.section .note.Xen, "a"
.align 4
    .long 4                      # namesz
    .long 4                      # descsz
    .long 18                     # type = XEN_ELFNOTE_PHYS32_ENTRY
    .asciz "Xen"
    .align 4
    .long _start                 # Entry point (32-bit physical address)

.section .text.boot
.global _start
.code32                         // PVH starts in 32-bit protected mode

_start:
    // Clear interrupts
    cli

    // Save multiboot info pointer (in EBX) to ESI
    mov %ebx, %esi

    // Set up page tables for long mode
    // Identity map first 4 MiB (covers kernel at 1 MiB + stack)

    // Clear page table area (4096 * 3 bytes starting at 0x1000)
    mov $0x1000, %edi
    mov $0x3000, %ecx
    xor %eax, %eax
    rep stosb

    // Set up PML4 (Page Map Level 4) at 0x1000
    mov $0x1000, %edi
    mov $0x2003, %eax           // Address of PDPT | Present | Writable
    mov %eax, (%edi)

    // Set up PDPT (Page Directory Pointer Table) at 0x2000
    mov $0x2000, %edi
    mov $0x3003, %eax           // Address of PD | Present | Writable
    mov %eax, (%edi)

    // Set up PD (Page Directory) at 0x3000
    // Identity map first 4 MiB using two 2 MiB pages
    mov $0x3000, %edi
    mov $0x0083, %eax           // Address 0 | Present | Writable | Huge page
    mov %eax, (%edi)
    add $8, %edi
    mov $0x200083, %eax         // Address 2 MiB | Present | Writable | Huge page
    mov %eax, (%edi)

    // Load CR3 with PML4 address
    mov $0x1000, %eax
    mov %eax, %cr3

    // Enable PAE (Physical Address Extension) in CR4
    mov %cr4, %eax
    or $0x20, %eax              // Set PAE bit (bit 5)
    mov %eax, %cr4

    // Enable Long Mode in EFER MSR
    mov $0xC0000080, %ecx       // EFER MSR number
    rdmsr
    or $0x100, %eax             // Set LME bit (bit 8)
    wrmsr

    // Load GDT
    lgdt gdt_descriptor

    // Enable paging in CR0 to activate long mode
    mov %cr0, %eax
    or $0x80000001, %eax        // Set PG bit (bit 31) and PE bit (bit 0)
    mov %eax, %cr0

    // Now we're in compatibility mode, need far jump to enter 64-bit mode
    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    // Now in 64-bit long mode with x32 ABI (32-bit pointers)!

    // Set up segment registers
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    // Set up stack pointer (x32 ABI requires 16-byte alignment like x64)
    lea stack_top(%rip), %rsp
    and $-16, %rsp              // Align to 16 bytes
    sub $8, %rsp                // Adjust so after call we're 16-byte aligned

    // Restore saved info pointer and pass to main
    // x32 calling convention: first arg in EDI (32-bit pointer, not RDI!)
    mov %esi, %edi

    // Clear direction flag
    cld

    // Branch to C entry point
    call main

    // If main returns, hang
hang:
    hlt
    jmp hang

// GDT for long mode (must be in .text section, not .text.boot)
.section .rodata
.align 16
gdt_start:
    .quad 0x0000000000000000    // Null descriptor
    .quad 0x00209A0000000000    // Code segment (64-bit, L=1, D=0)
    .quad 0x0000920000000000    // Data segment (64-bit)
gdt_end:

.align 16
gdt_descriptor:
    .word gdt_end - gdt_start - 1
    .long gdt_start

// Stack space (16 KiB)
.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

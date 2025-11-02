// x64 Boot Assembly - PVH boot entry (32-bit mode)

#include "platform_config.h"

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

    // Save PVH start info pointer (in EBX) to ESI
    // PVH boot protocol passes struct hvm_start_info* in EBX
    mov %ebx, %esi

    // Check if paging is already enabled (defensive check)
    mov %cr0, %eax
    test $0x80000000, %eax      // Test PG bit (bit 31)
    jnz paging_already_enabled  // If set, skip page table setup

    // Set up page tables for long mode
    // Identity map first 4GB with 2MB huge pages
    // Supports full 48-bit address space (256TB) via PML4 structure

    // Clear page table area (6 pages = 24 KiB)
    mov $_page_tables_start, %edi
    mov $_page_tables_end, %ecx
    sub %edi, %ecx
    xor %eax, %eax
    rep stosb

    // Calculate base address from linker symbol
    mov $_page_tables_start, %ebx

    // Set up PML4[0] -> PDPT (covers first 512GB of 256TB address space)
    mov %ebx, %edi              // PML4 at offset 0
    mov %ebx, %eax
    add $0x1000, %eax           // PDPT at offset 4096
    or $0x03, %eax              // Present | Writable
    mov %eax, (%edi)

    // Set up PDPT[0-3] -> PD0-PD3 (maps 0-4GB)
    mov %ebx, %edi
    add $0x1000, %edi           // PDPT at offset 4096
    mov %ebx, %eax
    add $0x2000, %eax           // PD0 at offset 8192
    or $0x03, %eax
    mov %eax, (%edi)            // PDPT[0] -> PD0 (0-1GB)

    add $8, %edi
    add $0x1000, %eax           // PD1 at offset 12288
    mov %eax, (%edi)            // PDPT[1] -> PD1 (1-2GB)

    add $8, %edi
    add $0x1000, %eax           // PD2 at offset 16384
    mov %eax, (%edi)            // PDPT[2] -> PD2 (2-3GB)

    add $8, %edi
    add $0x1000, %eax           // PD3 at offset 20480
    mov %eax, (%edi)            // PDPT[3] -> PD3 (3-4GB)

    // Fill PD0 (0-1GB): 512 × 2MB huge pages
    mov %ebx, %edi
    add $0x2000, %edi           // PD0 at offset 8192
    xor %eax, %eax              // Start at physical address 0
    mov $512, %ecx
.Lmap_pd0:
    mov %eax, %edx
    or $0x83, %edx              // Present | Writable | Huge
    mov %edx, (%edi)
    add $8, %edi
    add $0x200000, %eax         // Next 2MB
    loop .Lmap_pd0

    // Fill PD1 (1-2GB): 512 × 2MB huge pages
    mov %ebx, %edi
    add $0x3000, %edi           // PD1 at offset 12288
    mov $0x40000000, %eax       // Start at 1GB
    mov $512, %ecx
.Lmap_pd1:
    mov %eax, %edx
    or $0x83, %edx
    mov %edx, (%edi)
    add $8, %edi
    add $0x200000, %eax
    loop .Lmap_pd1

    // Fill PD2 (2-3GB): 512 × 2MB huge pages
    mov %ebx, %edi
    add $0x4000, %edi           // PD2 at offset 16384
    mov $0x80000000, %eax       // Start at 2GB
    mov $512, %ecx
.Lmap_pd2:
    mov %eax, %edx
    or $0x83, %edx
    mov %edx, (%edi)
    add $8, %edi
    add $0x200000, %eax
    loop .Lmap_pd2

    // Fill PD3 (3-4GB): 512 × 2MB huge pages
    mov %ebx, %edi
    add $0x5000, %edi           // PD3 at offset 20480
    mov $0xC0000000, %eax       // Start at 3GB
    mov $512, %ecx
.Lmap_pd3:
    mov %eax, %edx
    or $0x83, %edx
    mov %edx, (%edi)
    add $8, %edi
    add $0x200000, %eax
    loop .Lmap_pd3

    // Load CR3 with PML4 address (from linker symbol)
    mov $_page_tables_start, %eax
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

paging_already_enabled:
    // Paging is now enabled (either we just enabled it, or it was already on)
    // Now we're in compatibility mode, need far jump to enter 64-bit mode
    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    // Now in 64-bit long mode!

    // Set up segment registers
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    // Set up stack pointer (x64 ABI requires 16-byte alignment)
    lea stack_top(%rip), %rsp
    and $-16, %rsp              // Align to 16 bytes
    sub $8, %rsp                // Adjust so after call we're 16-byte aligned

    // Clear direction flag (required for string operations)
    cld

    // Zero BSS section (required for C runtime)
    // Note: Page tables are in BSS but must not be zeroed (already set up)
    // Only zero from __bss_start to _page_tables_start
    lea __bss_start(%rip), %rdi
    lea _page_tables_start(%rip), %rcx
    sub %rdi, %rcx              // Calculate BSS size up to page tables
    xor %rax, %rax              // Zero value
    rep stosb                   // memset(bss_start, 0, size_before_page_tables)

    // Zero BSS after page tables (from _page_tables_end to __bss_end)
    lea _page_tables_end(%rip), %rdi
    lea __bss_end(%rip), %rcx
    sub %rdi, %rcx              // Calculate BSS size after page tables
    xor %rax, %rax              // Zero value
    rep stosb                   // memset(page_tables_end, 0, size_after_page_tables)

    // Restore PVH start info pointer and pass to kmain
    // x64 calling convention: first argument in RDI
    // kmain(struct hvm_start_info *pvh_info)
    mov %rsi, %rdi

    // Branch to C entry point
    call kmain

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
    .word gdt_end - gdt_start - 1  // Limit (2 bytes)
    .quad gdt_start                 // Base address (8 bytes) - must be 64-bit for long mode

// Stack space (64 KiB) - placed at end of BSS
.section .bss
.align 16
.global stack_bottom
.global stack_top
stack_bottom:
    .skip 65536
stack_top:

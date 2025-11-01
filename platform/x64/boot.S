// x64 Boot Assembly - PVH boot entry (32-bit mode)

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
    // Also map high MMIO region for LAPIC, IOAPIC, etc.

    // Clear page table area (5 * 4096 bytes starting at 0x9000)
    // PML4 at 0x9000, PDPT at 0xA000, PD0 at 0xB000, PD3 at 0xC000, PD_MMIO at 0xD000
    // Using 0x9000+ avoids conflicts with BIOS data area (0x0000-0x7FFF)
    mov $0x9000, %edi
    mov $0x5000, %ecx
    xor %eax, %eax
    rep stosb

    // Set up PML4 (Page Map Level 4) at 0x9000
    mov $0x9000, %edi
    mov $0xA003, %eax           // Address of PDPT | Present | Writable
    mov %eax, (%edi)

    // Set up PDPT (Page Directory Pointer Table) at 0xA000
    mov $0xA000, %edi
    mov $0xB003, %eax           // PDPT[0] -> PD at 0xB000 (for 0-1GB)
    mov %eax, (%edi)
    add $24, %edi               // PDPT[3] (3 * 8 bytes)
    mov $0xC003, %eax           // PDPT[3] -> PD at 0xC000 (for 3-4GB, MMIO region)
    mov %eax, (%edi)

    // Set up PD (Page Directory) at 0xB000 for first 1GB
    // Identity map first 4 MiB using two 2 MiB pages
    mov $0xB000, %edi
    mov $0x0083, %eax           // Address 0 | Present | Writable | Huge page
    mov %eax, (%edi)
    add $8, %edi
    mov $0x200083, %eax         // Address 2 MiB | Present | Writable | Huge page
    mov %eax, (%edi)

    // Set up PD at 0xC000 for MMIO region (3-4GB range, PDPT[3])
    // PDPT[3] covers 3GB-4GB (0xC0000000 - 0xFFFFFFFF)
    // Map PCI MMIO range 0xC0000000-0xD0000000 (256 MiB) for PCI BARs
    // Map high MMIO range 0xFE000000-0xFF000000 (16 MiB) for LAPIC/IOAPIC

    // First, map PCI BAR region: 0xC0000000-0xD0000000 (128 x 2MB = 256MB)
    mov $0xC000, %edi           // PD base, entry 0
    mov $0xC000009B, %eax       // 0xC0000000 | Present | Writable | Huge page | PCD | PWT
    mov $128, %ecx              // Map 128 x 2 MiB pages = 256 MiB
.Lmap_pci_loop:
    mov %eax, (%edi)
    add $8, %edi                // Next PD entry
    add $0x200000, %eax         // Next 2 MiB physical address
    loop .Lmap_pci_loop

    // Second, map high MMIO region: 0xFE000000-0xFF000000 (8 x 2MB = 16MB)
    // This is PD entries 496-503 (496 * 8 = 3968 bytes offset)
    mov $0xC000, %edi
    add $3968, %edi             // PD[496]
    mov $0xFE00009B, %eax       // 0xFE000000 | Present | Writable | Huge page | PCD | PWT
    mov $8, %ecx                // Map 8 x 2 MiB pages = 16 MiB
.Lmap_mmio_loop:
    mov %eax, (%edi)
    add $8, %edi                // Next PD entry
    add $0x200000, %eax         // Next 2 MiB physical address
    loop .Lmap_mmio_loop

    // Load CR3 with PML4 address
    mov $0x9000, %eax
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
    lea __bss_start(%rip), %rdi
    lea __bss_end(%rip), %rcx
    sub %rdi, %rcx              // Calculate BSS size
    xor %rax, %rax              // Zero value
    rep stosb                   // memset(bss_start, 0, bss_size)

    // Restore saved info pointer and pass to main (x64 calling convention: first arg in RDI)
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

// Stack space (64 KiB)
.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:

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
    // Identity map first 4 MiB (covers page tables at 0x9000 and kernel at 2 MiB)
    // Also map high MMIO region for LAPIC, IOAPIC, etc.

    // Clear page table area starting at 0x100000 (1 MiB)
    // PML4 at 0x100000, PDPT at 0x101000, PD0 at 0x102000, PD3 at 0x103000, PT_kernel at 0x104000
    // Avoids conflicts with: BIOS (0x0-0x7FFF), EBDA (0x80000-0x9FFFF), old 0xC000 confusion
    // Total: 5 pages = 20 KiB
    mov $0x100000, %edi
    mov $0x5000, %ecx
    xor %eax, %eax
    rep stosb

    // Set up PML4 (Page Map Level 4) at 0x100000
    mov $0x100000, %edi
    mov $0x101003, %eax         // Address of PDPT | Present | Writable
    mov %eax, (%edi)

    // Set up PDPT (Page Directory Pointer Table) at 0x101000
    mov $0x101000, %edi
    mov $0x102003, %eax         // PDPT[0] -> PD at 0x102000 (for 0-1GB)
    mov %eax, (%edi)
    add $24, %edi               // PDPT[3] (3 * 8 bytes)
    mov $0x103003, %eax         // PDPT[3] -> PD at 0x103000 (for 3-4GB, MMIO region)
    mov %eax, (%edi)

    // Set up PD (Page Directory) at 0x102000 for first 1GB
    // First 2 MiB: Use huge page for low memory (BIOS, page tables, etc.)
    mov $0x102000, %edi
    mov $0x0083, %eax           // Address 0 | Present | Writable | Huge page
    mov %eax, (%edi)

    // Second 2 MiB (0x200000-0x3FFFFF): Use 4KB pages for kernel
    // This allows separate permissions for .text, .rodata, and .bss
    add $8, %edi
    mov $0x104003, %eax         // PT at 0x104000 | Present | Writable (NOT huge page)
    mov %eax, (%edi)

    // Set up PT (Page Table) at 0x104000 for kernel region (0x200000-0x3FFFFF)
    // Map 512 x 4KB pages = 2 MiB total with appropriate permissions:
    // - .text (0x200000-0x20CFFF): Read-only
    // - .rodata (0x20D000-0x20EFFF): Read-only
    // - .bss+stack (0x20F000+): Read-Write
    mov $0x104000, %edi         // PT base
    mov $0x200000, %eax         // Start at physical 0x200000
    mov $512, %ecx              // 512 entries

.Lmap_kernel_pages:
    mov %eax, %edx              // Copy physical address
    and $~0xFFF, %edx           // Clear offset bits

    // Check if this page needs R/W (bss starts at 0x20F000)
    cmp $0x20F000, %edx
    jge .Lkernel_rw             // >= 0x20F000: .bss, needs R/W

    // .text and .rodata: Read-only (Present only)
    or $0x01, %edx              // Present
    jmp .Lkernel_write_pte

.Lkernel_rw:
    // .bss and stack: Read-Write
    or $0x03, %edx              // Present | Writable

.Lkernel_write_pte:
    mov %edx, (%edi)
    add $8, %edi                // Next PT entry
    add $0x1000, %eax           // Next 4KB page
    loop .Lmap_kernel_pages

    // Set up PD at 0x103000 for MMIO region (3-4GB range, PDPT[3])
    // PDPT[3] covers 3GB-4GB (0xC0000000 - 0xFFFFFFFF)
    // Map PCI MMIO range 0xC0000000-0xD0000000 (256 MiB) for PCI BARs
    // Map high MMIO range 0xFE000000-0xFF000000 (16 MiB) for LAPIC/IOAPIC

    // First, map PCI BAR region: 0xC0000000-0xD0000000 (128 x 2MB = 256MB)
    mov $0x103000, %edi         // PD base, entry 0
    mov $0xC000009B, %eax       // 0xC0000000 | Present | Writable | Huge page | PCD | PWT
    mov $128, %ecx              // Map 128 x 2 MiB pages = 256 MiB
.Lmap_pci_loop:
    mov %eax, (%edi)
    add $8, %edi                // Next PD entry
    add $0x200000, %eax         // Next 2 MiB physical address
    loop .Lmap_pci_loop

    // Second, map high MMIO region: 0xFE000000-0xFF000000 (8 x 2MB = 16MB)
    // This is PD entries 496-503 (496 * 8 = 3968 bytes offset)
    mov $0x103000, %edi
    add $3968, %edi             // PD[496]
    mov $0xFE00009B, %eax       // 0xFE000000 | Present | Writable | Huge page | PCD | PWT
    mov $8, %ecx                // Map 8 x 2 MiB pages = 16 MiB
.Lmap_mmio_loop:
    mov %eax, (%edi)
    add $8, %edi                // Next PD entry
    add $0x200000, %eax         // Next 2 MiB physical address
    loop .Lmap_mmio_loop

    // Load CR3 with PML4 address
    mov $0x100000, %eax
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

// Stack space (64 KiB) - placed at end of BSS
.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:

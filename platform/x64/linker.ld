/* x64 Linker Script for QEMU
 * Load kernel at 2 MiB to avoid BIOS/firmware data structures
 */

ENTRY(_start)

SECTIONS
{
    /* Load kernel at 2 MiB physical address to avoid BIOS/firmware data structures.
     * This is the only hardcoded address - required by PVH boot protocol.
     * All other addresses are relative to this base. */
    . = 0x200000;

    /* Kernel image start (absolute beginning including PVH note) */
    _kernel_start = .;

    .note.Xen : {
      *(.note.Xen)
    }

    .text : {
        _text_start = .;
        *(.text.boot)
        *(.text*)
        _text_end = .;
    }

    /* Align rodata to page boundary for proper LOAD segment */
    . = ALIGN(4096);
    .rodata : {
        _rodata_start = .;
        *(.rodata*)
        _rodata_end = .;
    }

    /* Align data section to page boundary */
    . = ALIGN(4096);
    .data : {
        _data_start = .;
        *(.data*)
        _data_end = .;
    }

    /* Align BSS section to page boundary */
    . = ALIGN(4096);
    .bss : {
        __bss_start = .;
        _bss_start = .;
        *(.bss*)
        *(COMMON)

        /* Stack symbols defined in boot.S but declared here for reference */
        /* stack_bottom and stack_top are in the .bss section (boot.S) */

        /* Page tables region (24 KiB: PML4, PDPT, 4x PD for 4GB identity map)
         * Identity maps first 4GB using 2MB huge pages.
         * Supports full 48-bit address space (256TB) via PML4 structure.
         * Placed in BSS after stack to avoid hardcoded addresses.
         * Must be page-aligned for MMU configuration. */
        . = ALIGN(4096);
        _page_tables_start = .;
        . += 4096;  /* PML4 at offset 0 (covers 256TB) */
        . += 4096;  /* PDPT at offset 4096 (covers 0-512GB) */
        . += 4096;  /* PD0 at offset 8192 (0-1GB, 2MB pages) */
        . += 4096;  /* PD1 at offset 12288 (1-2GB, 2MB pages) */
        . += 4096;  /* PD2 at offset 16384 (2-3GB, 2MB pages) */
        . += 4096;  /* PD3 at offset 20480 (3-4GB, 2MB pages) */
        _page_tables_end = .;

        __bss_end = .;
        _bss_end = .;
    }

    /* End of kernel image (includes all sections: .text, .rodata, .data, .bss, stack, page tables) */
    . = ALIGN(16);
    _kernel_end = .;
    _end = .;  /* Alias for compatibility */
}

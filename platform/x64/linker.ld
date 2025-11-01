/* x64 Linker Script for QEMU
 * Load kernel at 2 MiB to avoid BIOS/firmware data structures
 */

ENTRY(_start)

SECTIONS
{
    /* Place kernel at 2 MiB to avoid BIOS data structures (ACPI tables, etc.) */
    . = 0x200000;

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
        __bss_end = .;
        _bss_end = .;
    }

    /* Stack symbols defined in boot.S but declared here for reference */
    /* stack_bottom and stack_top are in the .bss section (boot.S) */

    /* End of kernel */
    . = ALIGN(16);
    _end = .;
}

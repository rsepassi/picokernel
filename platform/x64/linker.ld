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
        *(.text.boot)
        *(.text*)
    }

    /* Align rodata to page boundary for proper LOAD segment */
    . = ALIGN(4096);
    .rodata : {
        *(.rodata*)
    }

    /* Align data section to page boundary */
    . = ALIGN(4096);
    .data : {
        *(.data*)
    }

    /* Align BSS section to page boundary */
    . = ALIGN(4096);
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /* End of kernel */
    . = ALIGN(16);
    _end = .;
}

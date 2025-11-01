/* x64 Linker Script for QEMU
 * Load kernel at 1 MiB
 */

ENTRY(_start)

SECTIONS
{
    /* Place kernel at 1 MiB */
    . = 0x100000;

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

    /* Align data section to prevent gaps */
    . = ALIGN(16);
    .data : {
        *(.data*)
    }

    /* Align BSS section properly */
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

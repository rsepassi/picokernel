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

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    /* End of kernel */
    . = ALIGN(16);
    _end = .;
}

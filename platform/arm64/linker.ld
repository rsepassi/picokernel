/* ARM64 Linker Script for QEMU virt machine
 * RAM starts at 0x40000000
 */

ENTRY(_start)

SECTIONS
{
    /* Place kernel at start of RAM */
    . = 0x40000000;

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

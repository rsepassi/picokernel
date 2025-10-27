/* RISC-V 64-bit Linker Script for QEMU virt machine
 * RAM starts at 0x80000000
 */

ENTRY(_start)

SECTIONS
{
    /* Place kernel after OpenSBI firmware */
    . = 0x80200000;

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

/* RISC-V 64-bit Linker Script for QEMU virt machine
 * RAM starts at 0x80000000
 */

ENTRY(_start)

SECTIONS
{
    /* Place kernel after OpenSBI firmware */
    . = 0x80200000;
    _start = .;

    _text_start = .;
    .text : {
        *(.text.boot)
        *(.text*)
    }
    _text_end = .;

    . = ALIGN(8);
    _rodata_start = .;
    .rodata : {
        *(.rodata*)
    }
    _rodata_end = .;

    . = ALIGN(8);
    _data_start = .;
    .data : {
        *(.data*)
    }
    _data_end = .;

    . = ALIGN(8);
    _bss_start = .;
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }
    _bss_end = .;

    /* End of kernel */
    . = ALIGN(16);
    _end = .;
}

/* RISC-V 32-bit Linker Script for QEMU virt machine
 * RAM starts at 0x80000000
 */

ENTRY(_start)

SECTIONS
{
    /* Place kernel after OpenSBI at 2MB offset */
    . = 0x80200000;

    .text : {
        _text_start = .;
        *(.text.boot)
        *(.text*)
        _text_end = .;
    }

    .rodata : {
        _rodata_start = .;
        *(.rodata*)
        _rodata_end = .;
    }

    .data : {
        _data_start = .;
        *(.data*)
        _data_end = .;
    }

    .bss : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    }

    /* Stack (grows down from top of memory) */
    . = ALIGN(16);
    stack_bottom = .;
    . = . + 0x10000; /* 64 KiB stack */
    stack_top = .;

    /* End of kernel */
    . = ALIGN(16);
    _end = .;
}

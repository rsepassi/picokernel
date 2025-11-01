/* ARM32 Linker Script for QEMU virt machine
 * RAM starts at 0x40000000
 * DTB is placed at 0x40000000 by QEMU for ELF kernels
 * So we load kernel at 0x40200000 (2 MB offset) to avoid collision
 */

ENTRY(_start)

SECTIONS
{
    /* Place kernel at 2MB into RAM to leave space for DTB */
    . = 0x40200000;

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
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
        __bss_end = .;
    }

    /* Stack (64 KiB, grows down from top) */
    . = ALIGN(16);
    stack_bottom = .;
    . += 0x10000;  /* 64 KiB stack */
    stack_top = .;

    /* End of kernel */
    . = ALIGN(16);
    _end = .;
}

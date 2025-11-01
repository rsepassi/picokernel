/* ARM64 Linker Script for QEMU virt machine
 * RAM starts at 0x40000000
 * DTB is placed at 0x40000000 by QEMU for ELF kernels
 * So we load kernel at 0x40200000 (2 MB offset) to avoid collision
 */

ENTRY(_start)

SECTIONS
{
    /* Place kernel at 2MB into RAM to leave space for DTB */
    . = 0x40200000;

    _text_start = .;
    .text : {
        *(.text.boot)
        *(.text*)
    }
    _text_end = .;

    _rodata_start = .;
    .rodata : {
        *(.rodata*)
    }
    _rodata_end = .;

    _data_start = .;
    .data : {
        *(.data*)
    }
    _data_end = .;

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

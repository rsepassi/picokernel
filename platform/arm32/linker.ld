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
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /* End of kernel */
    . = ALIGN(16);
    _end = .;
}

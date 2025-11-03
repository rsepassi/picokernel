/* ARM64 Linker Script for QEMU virt machine
 * RAM starts at 0x40000000
 * DTB is placed at 0x40000000 by QEMU for ELF kernels
 * So we load kernel at 0x40200000 (2 MB offset) to avoid collision
 */

ENTRY(_start)

SECTIONS
{
    /* Place kernel at 2MB into RAM to leave space for DTB.
     * This is the only hardcoded address - required to avoid DTB collision.
     * DTB region: 0x40000000 - 0x401FFFFF (2 MiB reserved)
     * Kernel base: 0x40200000 (all other addresses are relative to this base). */
    . = 0x40200000;

    .text : {
        _text_start = .;
        *(.text.boot)
        *(.text*)
        _text_end = .;
    }

    . = ALIGN(64);
    .rodata : {
        _rodata_start = .;
        *(.rodata*)
        _rodata_end = .;
    }

    . = ALIGN(64);
    .data : {
        _data_start = .;
        *(.data*)
        _data_end = .;
    }

    . = ALIGN(64);
    .bss : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)

        /* stack_bottom and stack_top are in the .bss section (boot.S) */

        _bss_end = .;
    }

    /* End of kernel */
    . = ALIGN(16);
    _end = .;
}

/* x32 Linker Script for QEMU
 * Load kernel at 1 MiB
 * x32 uses elf32-x86-64 format (64-bit instructions, 32-bit pointers)
 */

OUTPUT_FORMAT(elf32-x86-64)
OUTPUT_ARCH(i386:x64-32)
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

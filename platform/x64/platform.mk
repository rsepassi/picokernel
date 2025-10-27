# x64 platform configuration

TARGET = x86_64-none-elf

QEMU = qemu-system-x86_64
QEMU_MACHINE = q35
QEMU_CPU = max
QEMU_EXTRA_ARGS =

PLATFORM_CFLAGS = -mno-red-zone
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c devinfo.c acpi.c interrupt.c timer.c platform_init.c
PLATFORM_S_SRCS = boot.S isr.S

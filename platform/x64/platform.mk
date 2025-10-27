# x64 platform configuration

TARGET = x86_64-none-elf

QEMU = qemu-system-x86_64
QEMU_MACHINE = microvm
QEMU_CPU = max
QEMU_EXTRA_ARGS =

PLATFORM_CFLAGS = -mno-red-zone
PLATFORM_LDFLAGS =

# Additional platform-specific sources (beyond uart.c)
PLATFORM_ADDITIONAL_SRCS = devinfo.c acpi.c

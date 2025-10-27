# x32 platform configuration
# x32 is the x86-64 ABI with 32-bit pointers (ILP32 model)

TARGET = x86_64-none-elf

QEMU = qemu-system-x86_64
QEMU_MACHINE = microvm
QEMU_CPU = max
QEMU_EXTRA_ARGS =

PLATFORM_CFLAGS = -mx32 -mno-red-zone
PLATFORM_LDFLAGS = -m elf32_x86_64

# Additional platform-specific sources (beyond uart.c)
PLATFORM_ADDITIONAL_SRCS = devinfo.c acpi.c

# RISC-V 64-bit platform configuration

TARGET = riscv64-none-elf

QEMU = qemu-system-riscv64
QEMU_MACHINE = virt
QEMU_CPU = rv64
QEMU_EXTRA_ARGS = -bios none

PLATFORM_CFLAGS = -mcmodel=medany
PLATFORM_LDFLAGS =

# Shared sources from src/ that this platform uses
PLATFORM_SHARED_SRCS = devicetree.c

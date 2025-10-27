# RISC-V 32-bit platform configuration

TARGET = riscv32-none-elf

QEMU = qemu-system-riscv32
QEMU_MACHINE = virt
QEMU_CPU = rv32
QEMU_EXTRA_ARGS = -bios none

PLATFORM_CFLAGS = -mcmodel=medany
PLATFORM_LDFLAGS =

# Shared sources from src/ that this platform uses
PLATFORM_SHARED_SRCS = devicetree.c

# RISC-V 64-bit platform configuration

TARGET = riscv64-none-elf

QEMU = qemu-system-riscv64
QEMU_MACHINE = virt
QEMU_CPU = rv64
QEMU_EXTRA_ARGS = -bios default

PLATFORM_CFLAGS = -mcmodel=medany
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c platform_init.c interrupt.c timer.c sbi.c \
                  platform_hooks.c
PLATFORM_S_SRCS = boot.S trap.S

# Shared sources from platform/shared/ that this platform uses
PLATFORM_SHARED_SRCS = devicetree.c pci_ecam.c platform_virtio.c mmio.c

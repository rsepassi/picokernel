# RISC-V 64-bit platform configuration

TARGET = riscv64-none-elf

QEMU = qemu-system-riscv64
QEMU_MACHINE = virt
QEMU_CPU = rv64
QEMU_EXTRA_ARGS = -bios default

PLATFORM_CFLAGS = -mcmodel=medany
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c platform_core.c interrupt.c timer.c sbi.c \
                  platform_hooks.c platform_debug.c \
                  ../shared/devicetree.c ../shared/pci_ecam.c ../shared/platform_virtio.c \
                  ../shared/mmio.c ../shared/platform_checksums.c
PLATFORM_S_SRCS = boot.S trap.S

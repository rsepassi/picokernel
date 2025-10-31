# RISC-V 32-bit platform configuration

TARGET = riscv32-none-elf

QEMU = qemu-system-riscv32
QEMU_MACHINE = virt
QEMU_CPU = rv32
QEMU_EXTRA_ARGS = -bios default

PLATFORM_CFLAGS = -mcmodel=medany -Wno-gnu-statement-expression-from-macro-expansion
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c platform_init.c sbi.c timer.c interrupt.c \
                  platform_hooks.c pci.c platform_virtio.c runtime.c mmio.c
PLATFORM_S_SRCS = boot.S trap.S

# Shared sources from src/ that this platform uses
PLATFORM_SHARED_SRCS = devicetree.c

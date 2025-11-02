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
                  platform_hooks.c runtime.c platform_debug.c platform_mem_debug.c \
                  platform_boot_context.c \
                  ../shared/devicetree.c ../shared/mmio.c ../shared/pci_ecam.c \
                  ../shared/platform_virtio.c ../shared/platform_checksums.c
PLATFORM_S_SRCS = boot.S trap.S

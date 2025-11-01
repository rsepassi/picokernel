# ARM64 platform configuration

TARGET = aarch64-none-elf

QEMU = qemu-system-aarch64
QEMU_MACHINE = virt
QEMU_CPU = cortex-a57
QEMU_EXTRA_ARGS =

PLATFORM_CFLAGS = -mgeneral-regs-only
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c platform_init.c interrupt.c timer.c platform_debug.c platform_mem_debug.c \
                  ../shared/devicetree.c ../shared/pci_ecam.c ../shared/platform_virtio.c \
                  ../shared/mmio.c ../shared/platform_checksums.c
PLATFORM_S_SRCS = boot.S vectors.S

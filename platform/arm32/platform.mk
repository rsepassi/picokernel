# ARM32 platform configuration

TARGET = arm-none-eabi

QEMU = qemu-system-arm
QEMU_MACHINE = virt,highmem=off
QEMU_CPU = cortex-a15
QEMU_EXTRA_ARGS =

PLATFORM_CFLAGS = -march=armv7-a -mfloat-abi=soft -mfpu=none
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c platform_init.c interrupt.c timer.c runtime.c platform_debug.c \
                  platform_mem_debug.c platform_checksums.c \
                  ../shared/devicetree.c ../shared/mmio.c ../shared/pci_ecam.c ../shared/platform_virtio.c
PLATFORM_S_SRCS = boot.S vectors.S

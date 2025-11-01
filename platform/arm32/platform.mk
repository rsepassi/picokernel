# ARM32 platform configuration

TARGET = arm-none-eabi

QEMU = qemu-system-arm
QEMU_MACHINE = virt,highmem=off
QEMU_CPU = cortex-a15
QEMU_EXTRA_ARGS =

PLATFORM_CFLAGS = -march=armv7-a -mfloat-abi=soft -mfpu=none
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c platform_init.c interrupt.c timer.c runtime.c platform_debug.c
PLATFORM_S_SRCS = boot.S vectors.S

# Shared sources from platform/shared/ that this platform uses
PLATFORM_SHARED_SRCS = devicetree.c mmio.c pci_ecam.c platform_virtio.c

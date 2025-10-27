# ARM32 platform configuration

TARGET = arm-none-eabi

QEMU = qemu-system-arm
QEMU_MACHINE = virt
QEMU_CPU = cortex-a15
QEMU_EXTRA_ARGS =

PLATFORM_CFLAGS = -march=armv7-a
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c
PLATFORM_S_SRCS = boot.S

# Shared sources from src/ that this platform uses
PLATFORM_SHARED_SRCS = devicetree.c

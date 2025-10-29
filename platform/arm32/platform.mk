# ARM32 platform configuration

TARGET = arm-none-eabi

QEMU = qemu-system-arm
QEMU_MACHINE = virt,highmem=off
QEMU_CPU = cortex-a15

# VirtIO transport selection
# ARM32_USE_PCI=0: MMIO devices (default, simpler)
# ARM32_USE_PCI=1: PCI devices (tests ECAM support)
ARM32_USE_PCI ?= 0

ifeq ($(ARM32_USE_PCI),1)
  # ARM virt machine with VirtIO PCI devices
  QEMU_EXTRA_ARGS = -device virtio-rng-pci \
                    -drive file=$(IMG_FILE),if=none,id=hd0,format=raw,cache=none \
                    -device virtio-blk-pci,drive=hd0 \
                    -netdev user,id=net0 \
                    -device virtio-net-pci,netdev=net0
else
  # ARM virt machine with VirtIO MMIO devices (default)
  QEMU_EXTRA_ARGS = -device virtio-rng-device \
                    -drive file=$(IMG_FILE),if=none,id=hd0,format=raw,cache=none \
                    -device virtio-blk-device,drive=hd0 \
                    -netdev user,id=net0 \
                    -device virtio-net-device,netdev=net0
endif

PLATFORM_CFLAGS = -march=armv7-a -mfloat-abi=soft -mfpu=none
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c platform_init.c interrupt.c timer.c platform_hooks.c pci.c platform_virtio.c runtime.c
PLATFORM_S_SRCS = boot.S vectors.S

# Shared sources from src/ that this platform uses
PLATFORM_SHARED_SRCS = devicetree.c

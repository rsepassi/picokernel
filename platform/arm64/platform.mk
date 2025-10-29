# ARM64 platform configuration

TARGET = aarch64-none-elf

QEMU = qemu-system-aarch64
QEMU_MACHINE = virt
QEMU_CPU = cortex-a57

# VirtIO transport selection
# ARM64_USE_PCI=0: MMIO devices (default, simpler)
# ARM64_USE_PCI=1: PCI devices (tests ECAM support)
ARM64_USE_PCI ?= 0

ifeq ($(ARM64_USE_PCI),1)
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

PLATFORM_CFLAGS = -mgeneral-regs-only
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c platform_init.c interrupt.c timer.c platform_hooks.c pci.c platform_virtio.c
PLATFORM_S_SRCS = boot.S vectors.S

# Shared sources from src/ that this platform uses
PLATFORM_SHARED_SRCS = devicetree.c

# RISC-V 64-bit platform configuration

TARGET = riscv64-none-elf

QEMU = qemu-system-riscv64
QEMU_MACHINE = virt
QEMU_CPU = rv64

# VirtIO transport selection
# ARCH_USE_PCI=0: MMIO devices (default, simpler)
# ARCH_USE_PCI=1: PCI devices (tests ECAM support)
ARCH_USE_PCI ?= 0

ifeq ($(ARCH_USE_PCI),1)
  # Machine with VirtIO PCI devices
  QEMU_EXTRA_ARGS = -bios default \
                    -device virtio-rng-pci \
                    -drive file=$(IMG_FILE),if=none,id=hd0,format=raw,cache=none \
                    -device virtio-blk-pci,drive=hd0 \
                    -netdev user,id=net0 \
                    -device virtio-net-pci,netdev=net0
else
  # Machine with VirtIO MMIO devices (default)
  QEMU_EXTRA_ARGS = -bios default \
                    -device virtio-rng-device \
                    -drive file=$(IMG_FILE),if=none,id=hd0,format=raw,cache=none \
                    -device virtio-blk-device,drive=hd0 \
                    -netdev user,id=net0 \
                    -device virtio-net-device,netdev=net0
endif

PLATFORM_CFLAGS = -mcmodel=medany
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c platform_init.c interrupt.c timer.c sbi.c \
                  platform_hooks.c pci.c platform_virtio.c
PLATFORM_S_SRCS = boot.S trap.S

# Shared sources from src/ that this platform uses
PLATFORM_SHARED_SRCS = devicetree.c

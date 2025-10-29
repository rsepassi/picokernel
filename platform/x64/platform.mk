# x64 platform configuration

TARGET = x86_64-none-elf

QEMU = qemu-system-x86_64

# Machine type selection
# X64_USE_MICROVM=0: q35 with PCI devices (default, more compatible)
# X64_USE_MICROVM=1: microvm with MMIO devices (lightweight, fast boot)
X64_USE_MICROVM ?= 0

ifeq ($(X64_USE_MICROVM),1)
  # microvm machine with VirtIO MMIO devices
  QEMU_MACHINE = microvm
  QEMU_CPU = qemu64
  QEMU_EXTRA_ARGS = -device virtio-rng-device \
                    -drive file=$(IMG_FILE),if=none,id=hd0,format=raw,cache=none \
                    -device virtio-blk-device,drive=hd0 \
                    -netdev user,id=net0 \
                    -device virtio-net-device,netdev=net0
else
  # q35 machine with VirtIO PCI devices (default)
  QEMU_MACHINE = q35
  QEMU_CPU = qemu64
  QEMU_EXTRA_ARGS = -device virtio-rng-pci \
                    -drive file=$(IMG_FILE),if=none,id=hd0,format=raw,cache=none \
                    -device virtio-blk-pci,drive=hd0 \
                    -netdev user,id=net0 \
                    -device virtio-net-pci,netdev=net0
endif

PLATFORM_CFLAGS = -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-ssse3 -mno-sse4 -mno-sse4.1 -mno-sse4.2 -mno-avx -mno-avx2
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c devinfo.c acpi.c interrupt.c ioapic.c timer.c platform_init.c \
                  pci.c platform_hooks.c platform_virtio.c
PLATFORM_S_SRCS = boot.S isr.S

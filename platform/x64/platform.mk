# x64 platform configuration

TARGET = x86_64-none-elf

QEMU = qemu-system-x86_64

# Machine type selection (controlled by USE_PCI in main Makefile)
# USE_PCI=0: microvm with MMIO devices (lightweight, fast boot)
# USE_PCI=1: q35 with PCI devices (default, more compatible)
ifeq ($(USE_PCI),0)
  QEMU_MACHINE = microvm
  QEMU_CPU = qemu64
else
  QEMU_MACHINE = q35
  QEMU_CPU = qemu64
endif

QEMU_EXTRA_ARGS =

PLATFORM_CFLAGS = -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-ssse3 -mno-sse4 -mno-sse4.1 -mno-sse4.2 -mno-avx -mno-avx2
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c devinfo.c acpi.c interrupt.c ioapic.c timer.c platform_init.c \
                  pci.c platform_hooks.c platform_virtio.c
PLATFORM_S_SRCS = boot.S isr.S

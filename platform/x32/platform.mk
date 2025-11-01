# x32 platform configuration
# x32 is the x86-64 ABI with 32-bit pointers (ILP32 model)

TARGET = x86_64-none-elf

QEMU = qemu-system-x86_64

# Machine type selection (controlled by USE_PCI in main Makefile)
# USE_PCI=0: microvm with MMIO devices (lightweight, fast boot)
# USE_PCI=1: q35 with PCI devices (default, more compatible)
ifeq ($(USE_PCI),0)
  QEMU_MACHINE = microvm
  QEMU_CPU = max
else
  QEMU_MACHINE = q35
  QEMU_CPU = max
endif

QEMU_EXTRA_ARGS =

PLATFORM_CFLAGS = -mx32 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-ssse3 -mno-sse4 -mno-sse4.1 -mno-sse4.2 -mno-avx -mno-avx2
PLATFORM_LDFLAGS = -m elf32_x86_64

# Platform-specific sources (x32 only)
PLATFORM_C_SRCS = ../x86/uart.c ../x86/devinfo.c ../x86/acpi.c ../x86/interrupt.c ../x86/ioapic.c \
                  ../x86/timer.c ../x86/platform_init.c ../x86/pci.c ../x86/platform_hooks.c \
                  ../x86/platform_virtio.c ../x86/mmio.c ../x86/platform_mem_debug.c ../x86/platform_debug.c \
                  ../shared/platform_checksums.c
PLATFORM_S_SRCS = boot.S isr.S

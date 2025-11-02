# x64 platform configuration

TARGET = x86_64-none-elf

QEMU = qemu-system-x86_64
QEMU_CPU = qemu64

# Machine type selection (controlled by USE_PCI in main Makefile)
ifeq ($(USE_PCI),1)
  QEMU_MACHINE = q35
else
  QEMU_MACHINE = microvm
endif

QEMU_EXTRA_ARGS =

PLATFORM_CFLAGS = -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-ssse3 -mno-sse4 -mno-sse4.1 -mno-sse4.2 -mno-avx -mno-avx2
PLATFORM_LDFLAGS =

# Platform-specific sources (x64 only)
PLATFORM_C_SRCS = uart.c devinfo.c acpi.c interrupt.c ioapic.c \
                  timer.c platform_init.c pci.c platform_hooks.c \
                  platform_virtio.c mmio.c platform_boot_context.c \
                  platform_mem.c platform_mem_debug.c \
                  platform_debug.c ../shared/platform_checksums.c
PLATFORM_S_SRCS = boot.S isr.S

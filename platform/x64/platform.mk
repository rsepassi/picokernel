# x64 platform configuration

TARGET = x86_64-none-elf

QEMU = qemu-system-x86_64
QEMU_MACHINE = pc
QEMU_CPU = max
QEMU_EXTRA_ARGS = -device virtio-rng-pci

PLATFORM_CFLAGS = -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-ssse3 -mno-sse4 -mno-sse4.1 -mno-sse4.2 -mno-avx -mno-avx2
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c devinfo.c acpi.c interrupt.c ioapic.c timer.c platform_init.c \
                  pci.c pci_scan.c virtio_pci.c
PLATFORM_S_SRCS = boot.S isr.S

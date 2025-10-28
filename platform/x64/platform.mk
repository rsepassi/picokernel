# x64 platform configuration

TARGET = x86_64-none-elf

QEMU = qemu-system-x86_64
QEMU_MACHINE = microvm
QEMU_CPU = qemu64
QEMU_EXTRA_ARGS = -device virtio-rng-device \
                  -drive file=$(IMG_FILE),if=none,id=hd0,format=raw,cache=none \
                  -device virtio-blk-device,drive=hd0 \
                  -netdev user,id=net0 \
                  -device virtio-net-device,netdev=net0

PLATFORM_CFLAGS = -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-ssse3 -mno-sse4 -mno-sse4.1 -mno-sse4.2 -mno-avx -mno-avx2
PLATFORM_LDFLAGS =

# Platform-specific sources
PLATFORM_C_SRCS = uart.c devinfo.c acpi.c interrupt.c ioapic.c timer.c platform_init.c \
                  pci.c platform_hooks.c platform_virtio.c
PLATFORM_S_SRCS = boot.S isr.S

# vmos Makefile
# Build system for minimal OS kernel

# Default architecture
ARCH ?= arm64

# Build directory
BUILD_DIR = build/$(ARCH)

# Platform directory
PLATFORM_DIR = platform/$(ARCH)

# Include platform-specific configuration
include $(PLATFORM_DIR)/platform.mk

# Toolchain
CC = clang
LD = ld.lld

# Compiler flags
CFLAGS = --target=$(TARGET) -ffreestanding -nostdlib -Wall -Wextra -Werror -O2 $(PLATFORM_CFLAGS)
LDFLAGS = -nostdlib $(PLATFORM_LDFLAGS)

# Source files
SRC_DIR = src

BOOT_ASM = $(PLATFORM_DIR)/boot.S
LINKER_SCRIPT = $(PLATFORM_DIR)/linker.ld
PLATFORM_UART = $(PLATFORM_DIR)/uart.c
C_SOURCES = $(SRC_DIR)/main.c

# Object files in build directory
BOOT_OBJ = $(BUILD_DIR)/boot.o
UART_OBJ = $(BUILD_DIR)/uart.o
C_OBJECTS = $(BUILD_DIR)/main.o

ALL_OBJECTS = $(BOOT_OBJ) $(UART_OBJ) $(C_OBJECTS)

KERNEL = $(BUILD_DIR)/kernel.elf

.PHONY: all run clean
all: $(KERNEL)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BOOT_OBJ): $(BOOT_ASM) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(UART_OBJ): $(PLATFORM_UART) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(ALL_OBJECTS) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) $(ALL_OBJECTS) -o $@

run: $(KERNEL)
	$(QEMU) -machine $(QEMU_MACHINE) -cpu $(QEMU_CPU) \
		-m 128M -smp 1 \
		-nographic -nodefaults \
		-serial stdio \
		$(QEMU_EXTRA_ARGS) \
		-kernel $(KERNEL)

clean:
	rm -rf build


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

# Platform-specific C sources
PLATFORM_SRCS = $(PLATFORM_DIR)/uart.c $(PLATFORM_DIR)/devicetree.c

# Common C sources
C_SOURCES = $(SRC_DIR)/main.c

# Object files in build directory
BOOT_OBJ = $(BUILD_DIR)/boot.o
PLATFORM_OBJS = $(patsubst $(PLATFORM_DIR)/%.c,$(BUILD_DIR)/%.o,$(PLATFORM_SRCS))
C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))

ALL_OBJECTS = $(BOOT_OBJ) $(PLATFORM_OBJS) $(C_OBJECTS)

KERNEL = $(BUILD_DIR)/kernel.elf

.PHONY: all run clean
all: $(KERNEL)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BOOT_OBJ): $(BOOT_ASM) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) -c $< -o $@

$(BUILD_DIR)/%.o: $(PLATFORM_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) -c $< -o $@

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


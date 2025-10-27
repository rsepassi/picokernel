# vmos Makefile
# Build system for minimal OS kernel

# Default architecture
ARCH ?= arm64

# Build directory
BUILD_DIR = build/$(ARCH)

# Platform configuration
PLATFORM_DIR = platform/$(ARCH)
include $(PLATFORM_DIR)/platform.mk

# Toolchain
CC = clang
LD = ld.lld

# Compiler flags
CFLAGS = --target=$(TARGET) \
				 -std=c11 -Wpedantic -static -fno-pic -fno-pie \
				 -ffreestanding -nostdlib -fno-builtin \
				 -fwrapv -fno-strict-aliasing \
				 -Wall -Wextra -Werror \
				 -Wundef -Wmissing-prototypes -Wstrict-prototypes -Wvla -Wcast-align \
				 -O2 \
				 $(PLATFORM_CFLAGS)
LDFLAGS = -nostdlib $(PLATFORM_LDFLAGS)

# Source files
SRC_DIR = src

LINKER_SCRIPT = $(PLATFORM_DIR)/linker.ld

# Platform-specific sources (defined in platform.mk, with paths prepended)
PLATFORM_C_SOURCES = $(addprefix $(PLATFORM_DIR)/,$(PLATFORM_C_SRCS))
PLATFORM_S_SOURCES = $(addprefix $(PLATFORM_DIR)/,$(PLATFORM_S_SRCS))

# Shared sources (selected by platform.mk via PLATFORM_SHARED_SRCS)
SHARED_SOURCES = $(addprefix $(SRC_DIR)/,$(PLATFORM_SHARED_SRCS))

# Common C sources
C_SOURCES = $(SRC_DIR)/kmain.c $(SRC_DIR)/printk.c \
            $(SRC_DIR)/kernel.c $(SRC_DIR)/user.c

# VirtIO sources
VIRTIO_SOURCES = $(SRC_DIR)/virtio/virtio.c

# Object files in build directory
PLATFORM_C_OBJS = $(patsubst $(PLATFORM_DIR)/%.c,$(BUILD_DIR)/%.o,$(PLATFORM_C_SOURCES))
PLATFORM_S_OBJS = $(patsubst $(PLATFORM_DIR)/%.S,$(BUILD_DIR)/%.o,$(PLATFORM_S_SOURCES))
SHARED_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SHARED_SOURCES))
C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
VIRTIO_OBJECTS = $(patsubst $(SRC_DIR)/virtio/%.c,$(BUILD_DIR)/virtio/%.o,$(VIRTIO_SOURCES))

ALL_OBJECTS = $(PLATFORM_C_OBJS) $(PLATFORM_S_OBJS) $(SHARED_OBJS) $(C_OBJECTS) $(VIRTIO_OBJECTS)

KERNEL = $(BUILD_DIR)/kernel.elf

.PHONY: all run clean
all: $(KERNEL)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/virtio

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) -I$(SRC_DIR) -c $< -o $@

$(BUILD_DIR)/virtio/%.o: $(SRC_DIR)/virtio/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) -I$(SRC_DIR) -c $< -o $@

$(BUILD_DIR)/%.o: $(PLATFORM_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) -I$(SRC_DIR) -c $< -o $@

$(BUILD_DIR)/%.o: $(PLATFORM_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(ALL_OBJECTS) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) $(ALL_OBJECTS) -o $@

run: $(KERNEL)
	$(QEMU) -machine $(QEMU_MACHINE) -cpu $(QEMU_CPU) \
		-m 128M -smp 1 \
		-nographic -nodefaults \
		-serial stdio \
		$(QEMU_EXTRA_ARGS) \
		-kernel $(KERNEL) \
		-no-reboot

clean:
	rm -rf build

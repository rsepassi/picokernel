# vmos Makefile
# Build system for minimal OS kernel

# Options
ARCH ?= arm64
USE_PCI ?= 0  # PCI or MMIO
DRIVE ?= /tmp/drive.img
PORT ?= 5000

# Build directory
BUILD_DIR = build/$(ARCH)

# Platform configuration
PLATFORM_DIR = platform/$(ARCH)
include $(PLATFORM_DIR)/platform.mk

# Device flags for QEMU (unified across all platforms)
ifeq ($(USE_PCI),1)
  QEMU_DEVICE_ARGS = -device virtio-rng-pci \
                -drive file=$(DRIVE),if=none,id=hd0,format=raw,cache=none \
                -device virtio-blk-pci,drive=hd0 \
                -netdev user,id=net0,hostfwd=udp::$(PORT)-10.0.2.15:8080 \
                -device virtio-net-pci,netdev=net0
else
  QEMU_DEVICE_ARGS = -device virtio-rng-device \
                -drive file=$(DRIVE),if=none,id=hd0,format=raw,cache=none \
                -device virtio-blk-device,drive=hd0 \
                -netdev user,id=net0,hostfwd=udp::$(PORT)-10.0.2.15:8080 \
                -device virtio-net-device,netdev=net0
endif

# Compiler
CC = clang
CFLAGS = --target=$(TARGET) -O2 -static \
				 -ffreestanding -nostdlib -fno-builtin \
				 -fno-pic -fno-pie \
				 -fwrapv -fno-strict-aliasing \
				 -ffunction-sections -fdata-sections \
				 -std=c11 -Wpedantic \
				 -Wall -Wextra -Werror \
				 -Wundef -Wmissing-prototypes -Wstrict-prototypes -Wvla -Wcast-align \
				 $(PLATFORM_CFLAGS)

# Linker
LD = ld.lld
LDFLAGS = --no-pie -static -nostdlib --gc-sections $(PLATFORM_LDFLAGS)

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
            $(SRC_DIR)/kernel.c $(SRC_DIR)/user.c \
            $(SRC_DIR)/kcsprng.c \
            $(SRC_DIR)/kbase.c \
            $(SRC_DIR)/virtio/virtio.c \
            $(SRC_DIR)/virtio/virtio_mmio.c \
            $(SRC_DIR)/virtio/virtio_pci.c \
            $(SRC_DIR)/virtio/virtio_rng.c

# Vendor sources
VENDOR_DIR = vendor
VENDOR_SOURCES = $(VENDOR_DIR)/monocypher/monocypher.c \
                 $(VENDOR_DIR)/monocypher/monocypher-ed25519.c

# Header files (all .o files depend on all headers)
HEADERS = $(shell find $(SRC_DIR) $(PLATFORM_DIR) -name '*.h' 2>/dev/null)

# Object files in build directory (maintaining source tree structure)
PLATFORM_C_OBJS = $(patsubst $(PLATFORM_DIR)/%.c,$(BUILD_DIR)/platform/%.o,$(PLATFORM_C_SOURCES))
PLATFORM_S_OBJS = $(patsubst $(PLATFORM_DIR)/%.S,$(BUILD_DIR)/platform/%.o,$(PLATFORM_S_SOURCES))
SHARED_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/src/%.o,$(SHARED_SOURCES))
C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/src/%.o,$(C_SOURCES))
VENDOR_OBJECTS = $(patsubst $(VENDOR_DIR)/%.c,$(BUILD_DIR)/vendor/%.o,$(VENDOR_SOURCES))

ALL_OBJECTS = $(PLATFORM_C_OBJS) $(PLATFORM_S_OBJS) $(SHARED_OBJS) $(C_OBJECTS) $(VENDOR_OBJECTS)

KERNEL = $(BUILD_DIR)/kernel.elf

.PHONY: all run clean format
all: $(KERNEL)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/src/virtio
	mkdir -p $(BUILD_DIR)/platform
	mkdir -p $(BUILD_DIR)/vendor/monocypher

$(BUILD_DIR)/src/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) -I$(SRC_DIR) -I$(VENDOR_DIR) -c $< -o $@

$(BUILD_DIR)/vendor/%.o: $(VENDOR_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) -I$(SRC_DIR) -I$(VENDOR_DIR) -c $< -o $@

$(BUILD_DIR)/platform/%.o: $(PLATFORM_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) -I$(SRC_DIR) -c $< -o $@

$(BUILD_DIR)/platform/%.o: $(PLATFORM_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(ALL_OBJECTS) $(C_SOURCES) $(PLATFORM_C_SOURCES) $(SHARED_SOURCES) $(HEADERS) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) $(ALL_OBJECTS) -o $@

run: $(KERNEL)
	touch $(DRIVE)
	$(QEMU) -machine $(QEMU_MACHINE) -cpu $(QEMU_CPU) \
		-m 128M -smp 1 \
		-nographic -nodefaults -no-user-config \
		-serial stdio \
		$(QEMU_EXTRA_ARGS) \
		$(QEMU_DEVICE_ARGS) \
		-kernel $(KERNEL) \
		-no-reboot

clean:
	rm -rf build

format:
	clang-format -i $$(find src/ platform/ -type f \( -name '*.c' -o -name '*.h' \))

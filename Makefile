# vmos Makefile
# Build system for minimal OS kernel

# Options
PLATFORM ?= arm64
USE_PCI ?= 0
DRIVE ?= 0
PORT ?= 0
DEBUG ?= 0


# Use defaults if not overridden
ifeq ($(DRIVE),0)
	DRIVE_DEFAULT := /tmp/drive-$(shell date +%s)-$(shell jot -r 1 1000 9999).img
  DRIVE := $(DRIVE_DEFAULT)
endif
ifeq ($(PORT),0)
	PORT_DEFAULT := $(shell jot -r 1 49152 65535)
  PORT := $(PORT_DEFAULT)
endif

# Debug build configuration
ifeq ($(DEBUG),1)
  # Debug mode: easier stepping, full debug info, frame pointers for backtraces
  OPT_FLAGS := -O1
  DEBUG_CFLAGS := -g3 -fno-omit-frame-pointer -DKDEBUG
  DEBUG_LDFLAGS :=
  QEMU_DEBUG_FLAGS := -s -S -d guest_errors
else
  # Release mode: full optimizations, strip symbols, omit frame pointers
  OPT_FLAGS := -O2
  DEBUG_CFLAGS := -fomit-frame-pointer
  DEBUG_LDFLAGS := --strip-debug
  QEMU_DEBUG_FLAGS :=
endif

# Build directory
BUILD_DIR = build/$(PLATFORM)

# Platform configuration
PLATFORM_DIR = platform/$(PLATFORM)
include $(PLATFORM_DIR)/platform.mk

# Compiler
CC = clang
CFLAGS = --target=$(TARGET) $(OPT_FLAGS) $(DEBUG_CFLAGS) -static \
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
LDFLAGS = --no-pie -static -nostdlib --gc-sections $(DEBUG_LDFLAGS) $(PLATFORM_LDFLAGS)

# Common variables
INCLUDE_DIRS = -I$(PLATFORM_DIR) -I$(KERNEL_DIR) -I$(DRIVER_DIR) -I$(VENDOR_DIR)
PLATFORMS = rv32 rv64 x32 x64 arm32 arm64

# Source files
KERNEL_DIR = kernel
DRIVER_DIR = driver

LINKER_SCRIPT = $(PLATFORM_DIR)/linker.ld

# Platform-specific sources (defined in platform.mk, with paths prepended)
PLATFORM_C_SOURCES = $(addprefix $(PLATFORM_DIR)/,$(PLATFORM_C_SRCS))
PLATFORM_S_SOURCES = $(addprefix $(PLATFORM_DIR)/,$(PLATFORM_S_SRCS))

# Common C sources
C_SOURCES = $(KERNEL_DIR)/kmain.c $(KERNEL_DIR)/printk.c \
            $(KERNEL_DIR)/kernel.c $(KERNEL_DIR)/user.c \
            $(KERNEL_DIR)/kcsprng.c \
            $(KERNEL_DIR)/kbase.c \
            $(KERNEL_DIR)/irq_ring.c \
            $(KERNEL_DIR)/timer_heap.c \
            $(KERNEL_DIR)/mem_debug.c \
            $(KERNEL_DIR)/crc32.c \
            $(DRIVER_DIR)/virtio/virtio.c \
            $(DRIVER_DIR)/virtio/virtio_mmio.c \
            $(DRIVER_DIR)/virtio/virtio_pci.c \
            $(DRIVER_DIR)/virtio/virtio_rng.c \
            $(DRIVER_DIR)/virtio/virtio_blk.c \
            $(DRIVER_DIR)/virtio/virtio_net.c

# Vendor sources
VENDOR_DIR = vendor
VENDOR_SOURCES = $(VENDOR_DIR)/monocypher/monocypher.c \
                 $(VENDOR_DIR)/monocypher/monocypher-ed25519.c

# Header files (all .o files depend on all headers)
HEADERS = $(shell find $(KERNEL_DIR) $(DRIVER_DIR) $(PLATFORM_DIR) -name '*.h' 2>/dev/null)

# Object files in build directory (maintaining source tree structure)
# Platform sources may include files from ../shared or ../x86, so we match from platform/ root
PLATFORM_C_OBJS = $(patsubst platform/%.c,$(BUILD_DIR)/platform/%.o,$(PLATFORM_C_SOURCES))
PLATFORM_S_OBJS = $(patsubst platform/%.S,$(BUILD_DIR)/platform/%.o,$(PLATFORM_S_SOURCES))
KERNEL_OBJECTS = $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/kernel/%.o,$(filter $(KERNEL_DIR)/%,$(C_SOURCES)))
DRIVER_OBJECTS = $(patsubst $(DRIVER_DIR)/%.c,$(BUILD_DIR)/driver/%.o,$(filter $(DRIVER_DIR)/%,$(C_SOURCES)))
C_OBJECTS = $(KERNEL_OBJECTS) $(DRIVER_OBJECTS)
VENDOR_OBJECTS = $(patsubst $(VENDOR_DIR)/%.c,$(BUILD_DIR)/vendor/%.o,$(VENDOR_SOURCES))

ALL_OBJECTS = $(PLATFORM_C_OBJS) $(PLATFORM_S_OBJS) $(C_OBJECTS) $(VENDOR_OBJECTS)

KERNEL = $(BUILD_DIR)/kernel.elf

.PHONY: default run clean format test test-all flake flake-all debug-analyze debug-symbols debug-lldb
default: $(KERNEL)

all:
	for platform in $(PLATFORMS); do $(MAKE) PLATFORM=$$platform; done

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/{kernel,driver/virtio,platform,vendor/monocypher}

# Generic C compilation rule for kernel, driver, and vendor sources
$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

$(BUILD_DIR)/driver/%.o: $(DRIVER_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

$(BUILD_DIR)/vendor/%.o: $(VENDOR_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Platform-specific C sources (handles nested directories like ../shared/ and ../x86/)
$(BUILD_DIR)/platform/%.o: platform/%.c $(HEADERS) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# Platform-specific assembly sources
$(BUILD_DIR)/platform/%.o: platform/%.S | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(ALL_OBJECTS) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) $(ALL_OBJECTS) -o $@
ifeq ($(DEBUG),1)
	@echo "Computing and patching section checksums..."
	@./script/compute_checksums.py $@
endif

run: $(KERNEL) $(DRIVE)
	$(QEMU) -machine $(QEMU_MACHINE) -cpu $(QEMU_CPU) \
		-m 128M -smp 1 \
		-nographic -nodefaults -no-user-config \
		-serial stdio \
		$(QEMU_EXTRA_ARGS) \
		-device virtio-rng-$(if $(filter 1,$(USE_PCI)),pci,device) \
		-drive file=$(DRIVE),if=none,id=hd0,format=raw,cache=none \
		-device virtio-blk-$(if $(filter 1,$(USE_PCI)),pci,device),drive=hd0 \
		-netdev user,id=net0,hostfwd=udp::$(PORT)-10.0.2.15:8080 \
		-device virtio-net-$(if $(filter 1,$(USE_PCI)),pci,device),netdev=net0 \
		$(QEMU_DEBUG_FLAGS) \
		-kernel $(KERNEL) \
		-no-reboot

$(DRIVE):
	dd if=/dev/zero of=$@ bs=1M count=1

clean:
	rm -rf build

format:
	clang-format -i $$(find kernel/ driver/ platform/ -type f \( -name '*.c' -o -name '*.h' \))

test: $(KERNEL)
	@./script/run_test.sh $(PLATFORM) $(USE_PCI)

test-all:
	@./script/test_all.sh

flake: $(KERNEL)
	@./script/flake.sh $(PLATFORM) $(USE_PCI)

flake-all:
	@./script/flake_all.py

# Debug targets
debug-analyze: $(KERNEL)
	@echo "Analyzing kernel binary for $(PLATFORM)..."
	@./script/debug/analyze_elf.sh $(KERNEL)

debug-symbols: $(KERNEL)
	@echo "Generating symbol map for $(PLATFORM)..."
	@./script/debug/dump_symbols.sh $(KERNEL) > $(BUILD_DIR)/symbols.txt
	@echo "Symbol map written to $(BUILD_DIR)/symbols.txt"

debug-lldb: $(DRIVE)
	@echo "Starting debug session for $(PLATFORM)..."
	@echo "QEMU will start with GDB stub on port 1234"
	@echo "LLDB will attach automatically"
	@echo ""
	@$(MAKE) DEBUG=1 $(KERNEL)
	@./script/debug/debug_lldb.sh $(KERNEL) $(PLATFORM) $(USE_PCI) $(DRIVE) $(PORT)

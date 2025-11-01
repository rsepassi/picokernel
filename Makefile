# vmos Makefile
# Build system for minimal OS kernel

# Options
PLATFORM ?= arm64
USE_PCI ?= 0
DRIVE ?= 0
PORT ?= 0
DEBUG ?= 0

# Generate random defaults (immediate expansion)
DRIVE_DEFAULT := /tmp/drive-$(shell date +%s)-$(shell jot -r 1 1000 9999).img
PORT_DEFAULT := $(shell jot -r 1 49152 65535)

# Use defaults if not overridden
ifeq ($(DRIVE),0)
  DRIVE := $(DRIVE_DEFAULT)
endif
ifeq ($(PORT),0)
  PORT := $(PORT_DEFAULT)
endif

# Debug flags for QEMU (enable GDB stub)
ifeq ($(DEBUG),1)
  QEMU_DEBUG_FLAGS := -s -S
else
  QEMU_DEBUG_FLAGS :=
endif

# Build directory
BUILD_DIR = build/$(PLATFORM)

# Platform configuration
PLATFORM_DIR = platform/$(PLATFORM)
PLATFORM_SHARED_DIR = platform/shared
include $(PLATFORM_DIR)/platform.mk

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
KERNEL_DIR = kernel
DRIVER_DIR = driver

LINKER_SCRIPT = $(PLATFORM_DIR)/linker.ld

# Platform-specific sources (defined in platform.mk, with paths prepended)
PLATFORM_C_SOURCES = $(addprefix $(PLATFORM_DIR)/,$(PLATFORM_C_SRCS))
PLATFORM_S_SOURCES = $(addprefix $(PLATFORM_DIR)/,$(PLATFORM_S_SRCS))

# Shared sources (selected by platform.mk via PLATFORM_SHARED_SRCS)
PLATFORM_SHARED_SOURCES = $(addprefix $(PLATFORM_SHARED_DIR)/,$(PLATFORM_SHARED_SRCS))

# Common C sources
C_SOURCES = $(KERNEL_DIR)/kmain.c $(KERNEL_DIR)/printk.c \
            $(KERNEL_DIR)/kernel.c $(KERNEL_DIR)/user.c \
            $(KERNEL_DIR)/kcsprng.c \
            $(KERNEL_DIR)/kbase.c \
            $(KERNEL_DIR)/irq_ring.c \
            $(KERNEL_DIR)/timer_heap.c \
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
HEADERS = $(shell find $(KERNEL_DIR) $(DRIVER_DIR) $(PLATFORM_DIR) $(PLATFORM_SHARED_DIR) -name '*.h' 2>/dev/null)

# Object files in build directory (maintaining source tree structure)
PLATFORM_C_OBJS = $(patsubst $(PLATFORM_DIR)/%.c,$(BUILD_DIR)/platform/%.o,$(PLATFORM_C_SOURCES))
PLATFORM_S_OBJS = $(patsubst $(PLATFORM_DIR)/%.S,$(BUILD_DIR)/platform/%.o,$(PLATFORM_S_SOURCES))
PLATFORM_SHARED_OBJS = $(patsubst $(PLATFORM_SHARED_DIR)/%.c,$(BUILD_DIR)/shared/%.o,$(PLATFORM_SHARED_SOURCES))
KERNEL_OBJECTS = $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/kernel/%.o,$(filter $(KERNEL_DIR)/%,$(C_SOURCES)))
DRIVER_OBJECTS = $(patsubst $(DRIVER_DIR)/%.c,$(BUILD_DIR)/driver/%.o,$(filter $(DRIVER_DIR)/%,$(C_SOURCES)))
C_OBJECTS = $(KERNEL_OBJECTS) $(DRIVER_OBJECTS)
VENDOR_OBJECTS = $(patsubst $(VENDOR_DIR)/%.c,$(BUILD_DIR)/vendor/%.o,$(VENDOR_SOURCES))

ALL_OBJECTS = $(PLATFORM_C_OBJS) $(PLATFORM_S_OBJS) $(PLATFORM_SHARED_OBJS) $(C_OBJECTS) $(VENDOR_OBJECTS)

KERNEL = $(BUILD_DIR)/kernel.elf

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

.PHONY: default run clean format test test-all flake flake-all
default: $(KERNEL)

all:
	$(MAKE) PLATFORM=rv32
	$(MAKE) PLATFORM=rv64
	$(MAKE) PLATFORM=x32
	$(MAKE) PLATFORM=x64
	$(MAKE) PLATFORM=arm32
	$(MAKE) PLATFORM=arm64

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/kernel
	mkdir -p $(BUILD_DIR)/driver/virtio
	mkdir -p $(BUILD_DIR)/platform
	mkdir -p $(BUILD_DIR)/shared
	mkdir -p $(BUILD_DIR)/vendor/monocypher

$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) -I$(KERNEL_DIR) -I$(DRIVER_DIR) -I$(VENDOR_DIR) -c $< -o $@

$(BUILD_DIR)/driver/%.o: $(DRIVER_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) -I$(KERNEL_DIR) -I$(DRIVER_DIR) -I$(VENDOR_DIR) -c $< -o $@

$(BUILD_DIR)/vendor/%.o: $(VENDOR_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) -I$(KERNEL_DIR) -I$(DRIVER_DIR) -I$(VENDOR_DIR) -c $< -o $@

$(BUILD_DIR)/shared/%.o: $(PLATFORM_SHARED_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) -I$(KERNEL_DIR) -I$(DRIVER_DIR) -I$(VENDOR_DIR) -c $< -o $@

$(BUILD_DIR)/platform/%.o: $(PLATFORM_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) -I$(KERNEL_DIR) -I$(DRIVER_DIR) -c $< -o $@

$(BUILD_DIR)/platform/%.o: $(PLATFORM_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(ALL_OBJECTS) $(C_SOURCES) $(PLATFORM_C_SOURCES) $(PLATFORM_SHARED_SOURCES) $(HEADERS) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) -T $(LINKER_SCRIPT) $(ALL_OBJECTS) -o $@

run: $(KERNEL)
	test -f $(DRIVE) || dd if=/dev/zero of=$(DRIVE) bs=1M count=1
	$(QEMU) -machine $(QEMU_MACHINE) -cpu $(QEMU_CPU) \
		-m 128M -smp 1 \
		-nographic -nodefaults -no-user-config \
		-serial stdio \
		$(QEMU_EXTRA_ARGS) \
		$(QEMU_DEVICE_ARGS) \
		$(QEMU_DEBUG_FLAGS) \
		-kernel $(KERNEL) \
		-no-reboot

clean:
	rm -rf build

format:
	clang-format -i $$(find kernel/ driver/ platform/ -type f \( -name '*.c' -o -name '*.h' \))

test: $(KERNEL)
	@./script/run_test.sh $(PLATFORM) $(USE_PCI)

test-all:
	@echo "=== Running tests for all platforms and transports ==="
	@echo ""
	@FAILED=0; \
	PASSED=0; \
	for platform in arm64 arm32 x64 x32 rv64 rv32; do \
		for use_pci in 0 1; do \
			transport="MMIO"; \
			if [ "$$use_pci" = "1" ]; then transport="PCI"; fi; \
			echo ">>> Testing $$platform with $$transport..."; \
			if $(MAKE) test PLATFORM=$$platform USE_PCI=$$use_pci; then \
				PASSED=$$((PASSED + 1)); \
				echo "✓ $$platform ($$transport) PASSED"; \
			else \
				FAILED=$$((FAILED + 1)); \
				echo "✗ $$platform ($$transport) FAILED"; \
			fi; \
			echo ""; \
		done; \
	done; \
	echo "=== Test Summary ==="; \
	echo "Passed: $$PASSED / 12"; \
	echo "Failed: $$FAILED / 12"; \
	if [ "$$FAILED" -gt 0 ]; then exit 1; fi

flake: $(KERNEL)
	@echo "=== Flake test: $(PLATFORM) with $(if $(filter 1,$(USE_PCI)),PCI,MMIO) (10 runs) ==="
	@FAILED=0; \
	for i in 1 2 3 4 5 6 7 8 9 10; do \
		echo "[$$i/10] Testing..."; \
		if $(MAKE) test PLATFORM=$(PLATFORM) USE_PCI=$(USE_PCI) 2>&1 | grep -q "Overall: PASS"; then \
			echo "[$$i/10] ✓ PASSED"; \
		else \
			FAILED=$$((FAILED + 1)); \
			echo "[$$i/10] ✗ FAILED"; \
		fi; \
	done; \
	echo "=== Results: $$((10 - FAILED))/10 passed ==="; \
	if [ "$$FAILED" -gt 0 ]; then exit 1; fi

flake-all:
	@./script/flake_all.py

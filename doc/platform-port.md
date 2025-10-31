# Platform Porting Guide

This document describes how to port the VMOS kernel to a new architecture. It documents the required files, APIs, and changes needed based on the arm64/x64 → arm32 port.

## Overview

A platform port involves:
1. Creating new platform-specific files
2. Updating existing platform files to match the new kernel structure
3. Implementing required platform APIs defined in `kernel/platform.h`
4. Configuring the build system

## Required New Files

### 1. `kconfig_platform.h`

Platform-specific configuration header. Currently minimal but provides a place for future compile-time configuration.

```c
// <ARCH> Platform Configuration
// Platform-specific configuration for <ARCH>

#pragma once

// Platform configuration options will go here
// Currently empty - all VirtIO transports are compiled in by default
```

### 2. PCI Support (`pci.h` and `pci.c`)

**Required for:** VirtIO PCI transport support

**pci.h** - Define PCI register offsets and VirtIO device IDs:
```c
#pragma once
#include "platform.h"

// PCI config space registers
#define PCI_REG_VENDOR_ID 0x00
#define PCI_REG_DEVICE_ID 0x02
#define PCI_REG_COMMAND 0x04
// ... (see platform/arm64/pci.h for complete list)

// VirtIO PCI vendor/device IDs
#define VIRTIO_PCI_VENDOR_ID 0x1AF4
#define VIRTIO_PCI_DEVICE_RNG_LEGACY 0x1005
#define VIRTIO_PCI_DEVICE_RNG_MODERN 0x1044
// ... (see platform/arm64/pci.h for complete list)
```

**pci.c** - Implement platform_pci_config_read/write functions:
- ARM platforms: Use ECAM (memory-mapped configuration space at 0x4010000000)
- x86 platforms: Use I/O ports (0xCF8/0xCFC)
- See `kernel/platform.h` for the required API

### 3. Platform Hooks (`platform_hooks.c`)

**Required for:** VirtIO DMA coherency and IRQ management

Implement:
- `platform_cache_clean()` - Flush CPU cache to RAM (before device reads)
- `platform_cache_invalidate()` - Discard CPU cache (before CPU reads device-written data)
- `platform_memory_barrier()` - Full memory barrier
- `platform_irq_register()` - Register interrupt handler
- `platform_irq_enable()` - Enable (unmask) specific IRQ

**Architecture-specific notes:**
- **x86-64**: Cache operations are no-ops (hardware coherency)
- **ARM32/ARM64**: Use `dc cvac` / `dc ivac` / `dsb sy` instructions
- **RISC-V**: Use `fence` instructions or cache management SBI calls

Example for ARM:
```c
void platform_cache_clean(void *addr, size_t size) {
  uintptr_t start = (uintptr_t)addr & ~(CACHE_LINE_SIZE - 1);
  uintptr_t end = (uintptr_t)addr + size;

  for (uintptr_t va = start; va < end; va += CACHE_LINE_SIZE) {
    __asm__ volatile("dc cvac, %0" ::"r"(va) : "memory");
  }
  __asm__ volatile("dsb sy" ::: "memory");
}
```

### 4. VirtIO Platform Integration (`platform_virtio.c`)

**Required for:** VirtIO device discovery and management

This file provides:
- `pci_scan_devices(platform_t *platform)` - Scan PCI bus for VirtIO devices
- `mmio_scan_devices(platform_t *platform)` - Probe MMIO addresses for VirtIO devices
- `platform_tick(platform_t *platform, kernel_t *k)` - Process deferred interrupt work
- `platform_submit(platform_t *platform, kwork_t *submissions, kwork_t *cancellations)` - Submit work to devices

**Key implementation details:**
- Static storage for VirtIO transports and devices (no dynamic allocation)
- IRQ handlers use deferred processing pattern (minimal work in IRQ context)
- Polling fallback if interrupts don't work
- See `platform/arm64/platform_virtio.c` as reference implementation

## Required File Updates

### 1. `platform_impl.h`

Update `platform_t` structure to include:
```c
typedef struct {
  // Existing platform-specific fields...

  // New required fields:
  virtio_rng_dev_t *virtio_rng; // VirtIO-RNG device (NULL if not present)
  kernel_t *kernel;             // Back-pointer to kernel
} platform_t;

// Add RNG request platform-specific fields:
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;
```

### 2. `platform_init.c`

Update initialization sequence:

**Old pattern:**
```c
void platform_init(platform_t *platform, void *fdt) {
  platform->last_interrupt = PLATFORM_INT_UNKNOWN;
  interrupt_init();
  timer_init();
  interrupt_enable();  // ← Interrupts enabled early
  platform_fdt_dump(fdt);
}
```

**New pattern:**
```c
void platform_init(platform_t *platform, void *fdt) {
  platform->virtio_rng = NULL;

  interrupt_init();
  timer_init();

  // NOTE: Interrupts NOT enabled yet - will be enabled in event loop
  // to avoid spurious interrupts during device enumeration

  platform_fdt_dump(fdt);

  // Scan for VirtIO devices via both PCI and MMIO
  printk("=== Starting VirtIO Device Scan ===\n\n");
  pci_scan_devices(platform);
  mmio_scan_devices(platform);

  printk("\nPlatform initialization complete.\n\n");
}
```

**Key changes:**
- Initialize `platform->virtio_rng = NULL`
- Add forward declarations for `pci_scan_devices()` and `mmio_scan_devices()`
- Call device scan functions
- Interrupts are enabled later by the event loop

Update `platform_wfi()` signature and implementation:

**Old signature:**
```c
uint32_t platform_wfi(platform_t *platform, uint64_t timeout_ms);
```

**New signature:**
```c
uint64_t platform_wfi(platform_t *platform, uint64_t timeout_ms);
```

**New implementation pattern:**
```c
uint64_t platform_wfi(platform_t *platform, uint64_t timeout_ms) {
  if (timeout_ms == 0) {
    return timer_get_current_time_ms();
  }

  // Disable interrupts atomically
  __asm__ volatile("/* disable IRQs */");

  // Check if interrupt already pending
  virtio_rng_dev_t *rng = platform->virtio_rng;
  if (rng != NULL && rng->irq_pending) {
    __asm__ volatile("/* enable IRQs */");
    return timer_get_current_time_ms();
  }

  // Set timeout timer if not UINT64_MAX
  if (timeout_ms != UINT64_MAX) {
    uint32_t timeout_ms_32 = (timeout_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)timeout_ms;
    timer_set_oneshot_ms(timeout_ms_32, wfi_timer_callback);
  }

  // Atomically enable interrupts and wait
  __asm__ volatile("/* enable IRQs and WFI/HLT */");

  return timer_get_current_time_ms();
}
```

### 3. `timer.h` and `timer.c`

Add `timer_get_current_time_ms()` function:

**timer.h:**
```c
// Get current time in milliseconds
uint64_t timer_get_current_time_ms(void);
```

**timer.c implementation:**
1. Add static start time variable: `static uint64_t g_timer_start = 0;`
2. In `timer_init()`, capture start time: `g_timer_start = read_counter();`
3. Implement `timer_get_current_time_ms()`:

```c
uint64_t timer_get_current_time_ms(void) {
  uint64_t counter_now = read_counter();
  uint64_t counter_elapsed = counter_now - g_timer_start;

  if (g_timer_freq == 0) {
    return 0;
  }

  // Convert counter ticks to milliseconds
  // ms = (ticks * 1000) / freq_hz
  return (counter_elapsed * 1000) / g_timer_freq;
}
```

### 4. `interrupt.h` and `interrupt.c`

Add IRQ registration and dispatch support:

**interrupt.h:**
```c
// Register IRQ handler
void irq_register(uint32_t irq_num, void (*handler)(void *), void *context);

// Enable (unmask) a specific IRQ
void irq_enable(uint32_t irq_num);

// Dispatch IRQ (called from exception handler)
void irq_dispatch(uint32_t irq_num);
```

**interrupt.c implementation:**

1. Add IRQ table:
```c
#define MAX_IRQS 1024

typedef struct {
  void *context;
  void (*handler)(void *context);
} irq_entry_t;

static irq_entry_t g_irq_table[MAX_IRQS];
```

2. Implement registration:
```c
void irq_register(uint32_t irq_num, void (*handler)(void *), void *context) {
  if (irq_num >= MAX_IRQS) return;

  g_irq_table[irq_num].handler = handler;
  g_irq_table[irq_num].context = context;

  // Configure edge/level triggering as needed
  // VirtIO MMIO interrupts are typically edge-triggered
}
```

3. Update interrupt handler to call `irq_dispatch()`:
```c
void interrupt_handler(...) {
  uint32_t irq = /* read from interrupt controller */;

  if (irq == TIMER_IRQ) {
    timer_handler();
  } else if (irq >= 1020) {
    // Spurious interrupt
    return;
  } else {
    irq_dispatch(irq);  // ← New: dispatch to registered handlers
  }

  // Send EOI
}
```

### 5. `platform.mk`

Update build configuration:

```makefile
# VirtIO transport selection
# ARCH_USE_PCI=0: MMIO devices (default, simpler)
# ARCH_USE_PCI=1: PCI devices (tests ECAM/PIO support)
ARCH_USE_PCI ?= 0

ifeq ($(ARCH_USE_PCI),1)
  # Machine with VirtIO PCI devices
  QEMU_EXTRA_ARGS = -device virtio-rng-pci \
                    -drive file=$(IMG_FILE),if=none,id=hd0,format=raw,cache=none \
                    -device virtio-blk-pci,drive=hd0 \
                    -netdev user,id=net0 \
                    -device virtio-net-pci,netdev=net0
else
  # Machine with VirtIO MMIO devices (default)
  QEMU_EXTRA_ARGS = -device virtio-rng-device \
                    -drive file=$(IMG_FILE),if=none,id=hd0,format=raw,cache=none \
                    -device virtio-blk-device,drive=hd0 \
                    -netdev user,id=net0 \
                    -device virtio-net-device,netdev=net0
endif

# Platform-specific sources
PLATFORM_C_SRCS = uart.c platform_init.c interrupt.c timer.c \
                  platform_hooks.c pci.c platform_virtio.c
```

**Add runtime library if needed** (ARM32 example):
```makefile
PLATFORM_C_SRCS += runtime.c  # For EABI division functions
```

### 6. `boot.S`

Change entry point from `main` to `kmain`:

**Old:**
```asm
bl main
```

**New:**
```asm
bl kmain
```

### 7. `uart.c`

Add include for platform API:

```c
#include "platform.h"
#include <stdint.h>
```

This provides prototypes for `platform_uart_putc()` and `platform_uart_puts()`.

## Platform-Specific Runtime Libraries

### ARM32 EABI Division Functions

ARM32 requires software division functions. Create `runtime.c`:

```c
// ARM32 Runtime Library Functions
#include <stdint.h>

typedef struct {
  uint64_t quot;
  uint64_t rem;
} uldiv_t;

typedef struct {
  uint32_t quot;
  uint32_t rem;
} uidiv_t;

uldiv_t __aeabi_uldivmod(uint64_t numerator, uint64_t denominator);
uidiv_t __aeabi_uidivmod(uint32_t numerator, uint32_t denominator);
uint32_t __aeabi_uidiv(uint32_t numerator, uint32_t denominator);

// Implement using simple long division algorithm
// (See platform/arm32/runtime.c for full implementation)
```

### Other Architectures

- **ARM64**: No special runtime needed (has hardware division)
- **x86/x64**: No special runtime needed (has hardware division)
- **RISC-V**: May need M-extension emulation if M-extension not available

## Testing the Port

1. **Build test:**
   ```bash
   make ARCH=<your_arch>
   ```

2. **Run test:**
   ```bash
   make ARCH=<your_arch> run
   ```

3. **Expected boot output:**
   ```
   Initializing <ARCH> platform...
   <Interrupt controller initialized>
   <Timer initialized>
   === Starting VirtIO Device Scan ===

   Scanning PCI bus for VirtIO devices...
   Found VirtIO-RNG at PCI 0:X.0 (device ID 0xXXXX)

   Probing for VirtIO MMIO devices...
   Found VirtIO-RNG at MMIO 0x... (device ID 4)

   Platform initialization complete.
   ```

4. **Verify RNG works:**
   - Kernel should successfully request random data
   - VirtIO-RNG interrupts should fire (or polling should work)

## Common Issues

### 1. Missing division functions

**Symptom:** `undefined symbol: __aeabi_uldivmod` (or similar)

**Solution:** Implement runtime division functions or link against compiler-rt/libgcc

### 2. Interrupts not working

**Symptom:** "Polling fallback" messages, slow RNG

**Solutions:**
- Verify interrupt controller initialization
- Check IRQ routing (PCI INTx swizzling, MMIO IRQ mapping)
- Verify edge vs. level triggering configuration
- Check that interrupts are enabled globally

### 3. Cache coherency issues

**Symptom:** Corrupted data, device hangs

**Solutions:**
- Ensure `platform_cache_clean()` called before device reads memory
- Ensure `platform_cache_invalidate()` called before CPU reads device-written memory
- Verify cache line size is correct for your architecture

### 4. PCI devices not found

**Symptom:** "No VirtIO devices found" on PCI scan

**Solutions:**
- Verify ECAM base address matches your platform
- Check QEMU machine type supports PCI
- Verify BAR assignment for ARM platforms

## Reference Implementations

- **ARM64**: `platform/arm64/` - Full-featured reference with PCI and MMIO
- **x64**: `platform/x64/` - x86-specific I/O ports, ACPI
- **ARM32**: `platform/arm32/` - Example port following this guide

## Checklist

- [ ] Created `kconfig_platform.h`
- [ ] Created `pci.h` and `pci.c`
- [ ] Created `platform_hooks.c`
- [ ] Created `platform_virtio.c`
- [ ] Updated `platform_impl.h` (added virtio_rng, kernel fields)
- [ ] Updated `platform_init.c` (device scanning, new wfi signature)
- [ ] Updated `timer.h` and `timer.c` (added timer_get_current_time_ms)
- [ ] Updated `interrupt.h` and `interrupt.c` (IRQ registration/dispatch)
- [ ] Updated `platform.mk` (new sources, QEMU args)
- [ ] Updated `boot.S` (main → kmain)
- [ ] Updated `uart.c` (include platform.h)
- [ ] Created runtime library if needed (ARM32, RISC-V)
- [ ] Build succeeds
- [ ] Boot test succeeds
- [ ] VirtIO-RNG device detected
- [ ] RNG requests complete successfully

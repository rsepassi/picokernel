# VMOS API Architecture

This document describes the API structure and organization of VMOS.

## Overview

VMOS uses a layered architecture with clear boundaries between components. The
API is organized into distinct headers, each serving a specific purpose in the
system hierarchy.

## Layered Architecture

```
┌─────────────────────────────────────────┐
│  Layer 4: Application (user.h)          │  User-level application code
├─────────────────────────────────────────┤
│  Layer 3: Kernel API (kapi.h, kernel.h) │  Kernel abstractions & services
├─────────────────────────────────────────┤
│  Layer 2: Platform (platform.h)         │  Platform abstraction layer
├─────────────────────────────────────────┤
│  Layer 1: Foundation (kbase.h)          │  Base types, macros, utilities
└─────────────────────────────────────────┘
```

### Dependency Flow

Dependencies flow **downward only** - upper layers may depend on lower layers,
but never the reverse.

```
user.h
  └─> kernel.h
        └─> kapi.h
              └─> platform.h
                    └─> platform_impl.h (per-platform)
                          └─> kbase.h
```

## Core Headers

### Layer 1: Foundation

#### `kbase.h`
**Purpose**: Foundation header providing basic utilities used throughout the codebase.

**Provides**:
- Standard includes (stdint.h, stddef.h, stdbool.h, limits.h)
- Container macros (KCONTAINER_OF)
- Alignment macros (KALIGN, KALIGN_BACK)
- Utility macros (KMIN, KMAX, KARRAY_SIZE, KBIT)
- Static assertions
- Memory operations (memcpy, memset) for freestanding environment

**Dependencies**: Standard C library headers only

**Used by**: All other headers

---

### Layer 2: Platform Abstraction

#### `platform.h`
**Purpose**: Complete contract that all platform implementations must provide. This is the **boundary** between hardware-specific and hardware-agnostic code.

**Provides** (8 sections):
1. **Device Tree (FDT)**: Flattened device tree structures and parsing
2. **PCI**: PCI configuration space access
3. **Platform Lifecycle**: Initialization and wait-for-interrupt
4. **Interrupt Control**: Global interrupt enable/disable
5. **UART**: Debug output primitives
6. **Memory/Cache Operations**: Cache management and memory barriers
7. **IRQ Management**: Interrupt registration and control
8. **Work Submission**: Asynchronous work submission to platform layer

**Dependencies**: kbase.h, platform_impl.h

**Implemented by**: Each platform directory (platform/arm64, platform/x64, etc.)

#### `platform_impl.h` (per-platform)
**Purpose**: Platform-specific type definitions and inline implementations.

**Provides**:
- `platform_t`: Platform-specific state structure
- Platform-specific request types (e.g., krng_req_platform_t)
- Embedded device structures (VirtIO devices, timers, etc.)

**Location**: platform/*/platform_impl.h (one per platform)

**Dependencies**: Device-specific headers (virtio, timer, etc.)

**Note**: Included by platform.h, not directly by application code

---

### Layer 3: Kernel Services

#### `kconfig.h`
**Purpose**: Compile-time configuration and feature flags.

**Provides**:
- Feature toggles
- Size limits and constants
- Configuration parameters

**Dependencies**: None (pure configuration)

#### `kapi.h`
**Purpose**: Kernel API exposed to the rest of the system. Primary interface for kernel services.

**Provides**:
- `kernel_t`: Kernel state structure (opaque to users)
- `kwork_t`: Work item structure for async operations
- Work submission functions (krng_request, etc.)
- Work completion callbacks

**Dependencies**: platform.h, kconfig.h

#### `kernel.h`
**Purpose**: Internal kernel implementation details and lifecycle functions.

**Provides**:
- `kmain_init()`: Initialize kernel
- `kmain_tick()`: Periodic kernel tick
- `kmain_next_delay()`: Get next timer deadline
- `kplatform_complete_work()`: Platform→kernel work completion

**Dependencies**: kapi.h

**Used by**: Platform code and main entry point (kmain.c)

---

### Layer 4: Application

#### `user.h`
**Purpose**: User application interface.

**Provides**:
- `kmain_usermain()`: Main user application entry point

**Dependencies**: kernel.h, kapi.h

**Note**: This is where application logic lives

---

## Device Subsystems

### VirtIO Subsystem

The VirtIO subsystem is **transport-agnostic** and does not depend on platform.h at the header level.

```
virtio.h              (Core virtqueue structures)
  ├─> virtio_mmio.h   (MMIO transport)
  ├─> virtio_pci.h    (PCI transport)
  └─> virtio_rng.h    (RNG device driver)
```

**Key principle**: VirtIO headers define types only. Implementation files (*.c) include platform.h to access platform functions (cache operations, memory barriers).

**Dependency isolation**:
- Headers: virtio_*.h → virtio.h (no platform dependency)
- Sources: virtio_*.c → virtio_*.h + platform.h (via kapi.h)
- Platform: platform_impl.h → virtio_*.h (embeds device structures)

This design prevents circular dependencies while allowing platform-specific implementations to embed VirtIO device structures.

---

## Naming Conventions

### Function Prefixes

| Prefix | Scope | Example | Location |
|--------|-------|---------|----------|
| `k*` | Kernel utilities | `kcontainer_of()` | kbase.h |
| `kmain_*` | Main kernel lifecycle | `kmain_init()` | kernel.h |
| `kplatform_*` | Platform→Kernel calls | `kplatform_complete_work()` | kernel.h |
| `platform_*` | Kernel→Platform calls | `platform_init()` | platform.h |
| `krng_*` | RNG work API | `krng_request()` | kapi.h |

### Type Suffixes

- `_t`: Typedef'd structures (e.g., `platform_t`, `kernel_t`, `kwork_t`)

---

## Platform Contract

Each platform implementation must provide:

### Required Functions
1. **Lifecycle**: `platform_init()`, `platform_wfi()`
2. **Interrupts**: `platform_interrupt_enable()`, `platform_interrupt_disable()`
3. **UART**: `platform_uart_putc()`, `platform_uart_puts()`
4. **Memory**: `platform_cache_clean()`, `platform_cache_invalidate()`, `platform_memory_barrier()`
5. **IRQ**: `platform_irq_register()`, `platform_irq_enable()`
6. **Work**: `platform_submit()`, `platform_tick()`

### Required Types
1. **platform_t**: Platform state structure (defined in platform_impl.h)
2. **krng_req_platform_t**: Platform-specific RNG request fields

### Optional Functions
1. **FDT**: `platform_fdt_dump()` (ARM platforms)
2. **PCI**: `platform_pci_config_read*()`, `platform_pci_config_write*()`,
   `platform_pci_read_bar()` (x86 platforms)

---

## Design Principles

### 1. Clear Boundaries
Each layer has a well-defined responsibility and interface. Cross-layer calls are explicit and unidirectional.

### 2. Platform Abstraction
All hardware-specific code lives below platform.h. Code above platform.h is portable across architectures.

### 3. No Circular Dependencies
Header inclusion is strictly hierarchical. Forward declarations are used where needed to break potential cycles.

### 4. Transport Agnostic
Device drivers (VirtIO) are independent of transport mechanism (MMIO vs PCI) and platform specifics.

### 5. Minimal Headers
Headers contain only what's necessary. Implementation details stay in .c files.

### 6. Freestanding Environment
No dependencies on hosted C library. All required utilities (memcpy, memset) are provided in kbase.h.

---

## Including Headers

### For Application Code (user.c)
```c
#include "user.h"      // Provides user API
#include "kapi.h"      // Provides work submission API
```

### For Kernel Code (kernel.c)
```c
#include "kernel.h"    // Provides kernel internals
#include "kapi.h"      // Provides kernel API types
```

### For Platform Code (platform_init.c)
```c
#include "platform.h"  // Provides platform interface
#include "kernel.h"    // Provides kernel callbacks
```

### For Device Drivers (virtio_*.c)
```c
#include "virtio_*.h"  // Device-specific header
#include "kapi.h"      // Provides platform.h transitively
```

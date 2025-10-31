# Global Variables Audit and Refactoring Plan

**Date**: 2025-10-29
**Status**: Investigation Complete - Refactoring Pending

## Executive Summary

Investigation of static and global variables in `kernel/` and `platform/` directories shows:

- ✅ **kernel/ directory is clean** - No global state, only static helper functions
- ⚠️ **platform/ implementations have extensive global state** that should be moved to `platform_t`

All platform-specific globals should be moved into the `platform_t` structure, which is already embedded in `kernel_t` at `kernel/kernel.h:20`.

## Current State Analysis

### kernel/ Directory - ✅ No Issues

All `.c` files in `kernel/` properly use:
- Static helper functions only (correct usage for internal helpers)
- No file-scope variables
- All state properly passed through `kernel_t` structure

The `kernel_t` structure (`kernel/kernel.h:19-34`) already contains all necessary state:
- `platform_t platform` - Platform-specific state
- Work queues (submit, cancel, ready, timer)
- `kcsprng_ctx rng` - CSPRNG state
- `ktime_t current_time_ms` - Kernel time

### platform/ Directory - ⚠️ Extensive Global State

All platforms (arm32, arm64, rv32, rv64, x32, x64) have similar patterns of global variables that should be refactored into `platform_t`.

## Global Variables Catalog

### 1. VirtIO Device State (All Platforms)

**Location**: `platform/*/platform_virtio.c`

```c
static virtio_pci_transport_t g_virtio_pci_transport;
static virtio_mmio_transport_t g_virtio_mmio_transport;
static virtio_rng_dev_t g_virtio_rng;
static volatile uint32_t g_irq_count;           // x32/x64 only
static volatile uint32_t g_last_isr_status;     // x32/x64 only
```

**Assessment**: High priority - these are per-instance device state
**Impact**: All VirtIO operations
**Affected Files**: `platform/*/platform_virtio.c`, `platform/*/platform_init.c`

### 2. Timer State (All Platforms)

**Location**: `platform/*/timer.c`

```c
// All platforms:
static timer_callback_t g_timer_callback;
static uint64_t g_timer_start;

// Platform-specific:
static uint64_t g_timer_freq_hz;               // arm64
static uint32_t g_timer_freq;                  // arm32, rv32
static uint64_t g_timebase_freq;               // rv64
static uint64_t g_lapic_base;                  // x32, x64
static uint32_t g_ticks_per_ms;                // x32, x64, arm32
static uint64_t g_tsc_start;                   // x64
static uint64_t g_tsc_per_ms;                  // x64
static uint64_t g_tsc_freq;                    // x32
static volatile int g_timer_fired;             // rv32
```

**Assessment**: High priority - timer state is per-platform instance
**Impact**: All timer operations, `platform_wfi()`
**Affected Files**: `platform/*/timer.c`, timer initialization

### 3. Interrupt State (All Platforms)

**Location**: `platform/*/interrupt.c`

```c
// All platforms:
static irq_entry_t g_irq_table[MAX_IRQS];
static const char *exception_names[];          // Can stay static const

// x86-specific (x32, x64):
static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;
static volatile uint32_t g_irq_dispatch_count; // x64 only
static volatile uint32_t g_irq_eoi_count;      // x64 only
```

**Assessment**: Medium priority - IRQ tables are large but rarely accessed outside interrupts
**Impact**: Interrupt registration and handling
**Affected Files**: `platform/*/interrupt.c`, ISR handlers

**Note**: `exception_names[]` arrays should remain as `static const` (read-only lookup tables)

### 4. ACPI State (x86 Platforms)

**Location**: `platform/x32/acpi.c`, `platform/x64/acpi.c`

```c
static struct acpi_rsdp *g_rsdp;
```

**Assessment**: Medium priority - cached ACPI root pointer
**Impact**: ACPI table lookups
**Affected Files**: `platform/x*/acpi.c`

### 5. I/O APIC State (x86 Platforms)

**Location**: `platform/x32/ioapic.c`, `platform/x64/ioapic.c`

```c
static ioapic_t g_ioapic;
```

**Assessment**: Medium priority - I/O APIC configuration
**Impact**: Interrupt routing
**Affected Files**: `platform/x*/ioapic.c`

### 6. Platform Pointer Anti-Pattern

**Location**: `platform/x32/platform_init.c`

```c
static platform_t *g_platform = NULL;
static volatile int g_wfi_done = 0;
```

**Assessment**: HIGH PRIORITY - defeats the purpose of passing `platform_t*`
**Impact**: Creates hidden global dependency
**Affected Files**: `platform/x32/platform_init.c`

## Refactoring Plan

### Priority Levels

#### High Priority (Breaks Modularity)
1. **Platform pointer anti-pattern** (`g_platform` in x32)
2. **VirtIO device state** (all platforms)
3. **Timer state** (all platforms)

#### Medium Priority (Reduces Reentrancy)
4. **Interrupt tables** (all platforms)
5. **ACPI state** (x86 platforms)
6. **I/O APIC state** (x86 platforms)

#### Low Priority (Debug/Stats)
7. **Debug counters** (`g_irq_dispatch_count`, etc.)

### Implementation Steps

#### Step 1: Define platform_t Structure

Each `platform/*/platform_impl.h` should define its `platform_t` to include all platform-specific state:

```c
// Example: platform/x64/platform_impl.h
typedef struct {
    // VirtIO devices
    virtio_pci_transport_t virtio_pci;
    virtio_mmio_transport_t virtio_mmio;
    virtio_rng_dev_t virtio_rng;

    // Timer state
    timer_callback_t timer_callback;
    uint64_t timer_start;
    uint64_t lapic_base;
    uint32_t ticks_per_ms;
    uint64_t tsc_start;
    uint64_t tsc_per_ms;

    // Interrupt state
    irq_entry_t irq_table[MAX_IRQ_VECTORS];
    struct idt_entry idt[IDT_ENTRIES];
    struct idt_ptr idtp;

    // ACPI/IOAPIC
    struct acpi_rsdp *rsdp;
    ioapic_t ioapic;

    // Stats (optional)
    volatile uint32_t irq_count;
    volatile uint32_t irq_dispatch_count;
    volatile uint32_t irq_eoi_count;
    volatile uint32_t last_isr_status;
} platform_t;
```

#### Step 2: Update Function Signatures

Some platform-internal functions may need to accept `platform_t*` parameter:

```c
// Before:
void timer_init(void);

// After:
void timer_init(platform_t *platform);
```

#### Step 3: Update Access Patterns

Replace global access with structure member access:

```c
// Before:
g_timer_callback = callback;

// After:
platform->timer_callback = callback;
```

#### Step 4: Update ISR Context

Interrupt handlers that currently access globals will need platform context. Options:
- Store `platform_t*` in IRQ table entries (recommended)
- Accept that ISRs use a single global `platform_t` instance per CPU (pragmatic for embedded)

### Testing Strategy

1. Refactor one platform at a time (suggest starting with x64)
2. Ensure builds for all platforms after each platform is refactored
3. Test on QEMU for the refactored platform
4. Verify no behavior changes (same test output)

## Open Questions

1. **ISR Context**: Should we store `platform_t*` in IRQ table entries, or accept single-instance-per-CPU limitation for embedded systems?

2. **Platform Instance Count**: Are we planning to support multiple platform instances in the future? (Currently `kernel_t` has exactly one embedded `platform_t`)

3. **Cache Line Alignment**: Should large structures like IRQ tables be cache-aligned within `platform_t`?

4. **Static Const Data**: Confirm that read-only lookup tables (`exception_names[]`) should remain as `static const` and not be moved to `platform_t`

## Files Requiring Changes

### Per-Platform Changes (repeat for each of 6 platforms)

```
platform/*/platform_impl.h    - Define expanded platform_t
platform/*/platform_init.c    - Remove g_platform pointer, update init
platform/*/platform_virtio.c  - Remove globals, use platform->virtio_*
platform/*/timer.c            - Remove globals, use platform->timer_*
platform/*/interrupt.c        - Remove globals, use platform->irq_*
platform/x*/acpi.c           - Remove g_rsdp, use platform->rsdp (x86 only)
platform/x*/ioapic.c         - Remove g_ioapic, use platform->ioapic (x86 only)
```

### Verification Files (no changes needed)
```
kernel/kernel.h                  - Already embeds platform_t
kernel/kernel.c                  - Already passes platform_t
kernel/platform.h                - Interface remains unchanged
```

## Benefits of Refactoring

1. **Testability**: Could theoretically instantiate multiple platforms (useful for testing)
2. **Reentrancy**: Cleaner design even if only single instance used
3. **Code Clarity**: Explicit dependencies vs hidden globals
4. **Future-Proof**: Easier to support multi-core or virtualization scenarios

## Notes

- The `kernel/` directory is exemplary - no refactoring needed
- This refactoring is mechanical but touches many files
- Consider using preprocessor macros during transition to support both old/new code
- Total files affected: ~40-50 files across 6 platforms

---

## ARM64 Refactoring Results (Completed 2025-10-29)

### Summary

ARM64 has been successfully refactored to move all globals into `platform_t`. The platform boots, runs VirtIO-RNG, and passes all functional tests.

### Key Changes

**1. Platform Structure Updates** (`platform/arm64/platform_impl.h`)
- Added timer state: `timer_freq_hz`, `timer_start`, `timer_callback`
- Added VirtIO device structures: `virtio_pci_transport`, `virtio_mmio_transport`, `virtio_rng`
- Added VirtIO queue memory: `virtqueue_memory_t virtqueue_memory` (properly typed, not raw bytes)
- Added IRQ table: `irq_entry_t irq_table[MAX_IRQS]` (1024 entries)

**2. Function Signature Updates**
All platform-internal functions now accept `platform_t*`:
- `timer_init(platform_t*)`, `timer_set_oneshot_ms(platform_t*, ...)`, etc.
- `interrupt_init(platform_t*)`, `irq_register(platform_t*, ...)`, etc.
- VirtIO init functions now use `&platform->virtqueue_memory`

**3. Unavoidable Module-Local State**

One module-local pointer remains:
```c
// platform/arm64/interrupt.c:56
static platform_t *g_current_platform = NULL;
```

**Reason**: `exception_handler()` is called from assembly (`vectors.S`) and cannot receive parameters. This is an architectural limitation common to all platforms with exception vectors in assembly.

**Scope**: Only used for interrupt dispatch; not a general-purpose global.

**4. Stack Overflow Prevention** (`kernel/kmain.c`)

Moved `kernel_t` from stack to file-level static:
```c
static kernel_t g_kernel;
```

**Reason**: With IRQ table (1024 entries × 16 bytes = 16KB) and typed virtqueue memory (~16KB), `platform_t` is now ~32KB. Combined with other kernel state, stack allocation risked overflow.

**Alternative Considered**: Could have kept virtqueue_memory as module-local, but properly typing it (as `virtqueue_memory_t`) was deemed more important than avoiding file-level storage.

### Lessons for Other Platform Ports

**1. Header Organization**
- `platform_impl.h` needs complete type definitions (not forward declarations) for embedded structs
- Include `virtio/virtio.h` for `virtqueue_memory_t` type
- Define `timer_callback_t` and `irq_entry_t` in `platform_impl.h` to avoid circular dependencies

**2. Exception Handlers**
- Assembly-invoked exception handlers cannot easily receive `platform_t*`
- Acceptable solution: module-local pointer set during `interrupt_init()`
- Document this clearly as architectural limitation, not laziness

**3. Platform Hooks** (`platform_hooks.c`)
- Platform API functions (`platform_irq_register`, `platform_irq_enable`) don't have `platform_t*` in their signature
- Solution: Use `extern platform_t *g_current_platform` from interrupt.c
- Document that these hooks rely on interrupt subsystem initialization

**4. Queue Memory Typing**
- Use `virtqueue_memory_t` (defined in `virtio/virtio.h`) instead of raw byte arrays
- This provides type safety and documents the structure layout
- Acceptable to keep in `platform_t` despite size (~16KB) since kernel_t is now file-level

**5. Build Verification**
- Test incrementally: expand struct → update one subsystem → build → repeat
- Watch for typedef conflicts between headers (use include guards carefully)
- Large structs may require moving kernel_t to file-level storage

**6. Files Modified Per Platform** (arm64 example)
- `platform_impl.h` - Expand platform_t structure
- `timer.c`, `timer.h` - Add platform_t* parameter
- `interrupt.c`, `interrupt.h` - Add platform_t* parameter, add g_current_platform
- `platform_virtio.c` - Use platform->virtqueue_memory
- `platform_hooks.c` - Reference g_current_platform for API compatibility
- `platform_init.c` - Pass platform to subsystems

**7. Testing**
- Verify device discovery (PCI/MMIO scan)
- Verify interrupt delivery (timer + device IRQs)
- Verify VirtIO operations (RNG entropy generation)
- Check for stack overflows (watch boot behavior, add stack canaries if needed)

---

**Next Actions**:
1. ~~Review this document with team~~ (DONE)
2. ~~Choose pilot platform (recommend x64)~~ (DONE - used arm64)
3. ~~Create feature branch for refactoring~~ (DONE)
4. ~~Implement, test, and iterate~~ (DONE - arm64 complete)
5. **Apply learnings to remaining platforms: arm32, rv32, rv64, x32, x64**

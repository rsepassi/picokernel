# Porting the Async Runtime to New Platforms

## Overview

This document describes how to port the async work queue runtime and VirtIO-RNG device support to a new platform architecture. The async runtime enables interrupt-driven device I/O with deferred processing.

**Reference implementations:**
- **x64**: Complete, production-ready (IOAPIC, LAPIC, TSC, PCI via I/O ports)
- **arm64**: Complete, production-ready (GIC, Generic Timer, PCI via ECAM)

## Architecture

The async runtime uses a **deferred interrupt processing model**:

1. **ISR (minimal)**: Hardware interrupt → Set flag → Return
2. **platform_wfi()**: Wait for interrupt, return current time
3. **ktick()**: Process deferred work, run callbacks, submit new work
4. **Event loop**: Infinite `ktick() → platform_wfi()` cycle

## Required Platform Changes

### 1. Platform Structure (platform_impl.h)

Add kernel backpointer and device state:

```c
struct kernel;
typedef struct kernel kernel_t;

struct virtio_rng_t;
typedef struct virtio_rng_t virtio_rng_t;

typedef struct {
    kernel_t* kernel;          // Back-pointer to kernel
    virtio_rng_t* virtio_rng;  // VirtIO-RNG device (NULL if absent)
    // Platform-specific fields (timer freq, etc.)
} platform_t;

// Platform-specific fields for RNG requests
typedef struct {
    uint16_t desc_idx;  // VirtIO descriptor index
} krng_req_platform_t;
```

**Remove** obsolete `last_interrupt` field - no longer needed with async runtime.

**Reference files:**
- `platform/x64/platform_impl.h`
- `platform/arm64/platform_impl.h`

### 2. Platform Initialization (platform_init.c)

**Critical change**: Do NOT enable interrupts in `platform_init()`!

```c
void platform_init(platform_t* platform, void* fdt) {
    platform->virtio_rng = NULL;
    platform->kernel = NULL;  // Set by kmain

    // Initialize interrupt controller (but don't enable)
    interrupt_init();

    // Initialize timer
    timer_init();

    // NOTE: Interrupts NOT enabled yet - will be enabled in event loop
    // to avoid spurious interrupts during device enumeration

    // Scan for devices
    pci_scan_devices(platform);
}
```

Interrupts are enabled once by `kmain()` after device enumeration.

**Reference files:**
- `platform/x64/platform_init.c:23-43`
- `platform/arm64/platform_init.c:17-45`

### 3. Platform WFI (platform_init.c)

Change signature: Return **time in milliseconds**, not interrupt code.

```c
// Wait for interrupt with timeout
// timeout_ms: timeout in milliseconds (UINT64_MAX = wait forever)
// Returns: current time in milliseconds
uint64_t platform_wfi(platform_t* platform, uint64_t timeout_ms) {
    // Disable interrupts to check condition atomically
    interrupt_disable();

    // Check if work is already pending
    if (platform->virtio_rng && platform->virtio_rng->irq_pending) {
        interrupt_enable();
        return timer_get_current_time_ms();
    }

    // Set timeout timer if not UINT64_MAX
    if (timeout_ms != UINT64_MAX) {
        timer_set_oneshot_ms((uint32_t)timeout_ms, wfi_callback);
    }

    // Atomically enable interrupts and wait
    // (architecture-specific: x64 uses "sti; hlt", arm64 uses daifclr + wfi)
    interrupt_enable();
    wait_for_interrupt();  // Platform-specific

    // Return current time
    return timer_get_current_time_ms();
}
```

**Reference files:**
- `platform/x64/platform_init.c:48-74`
- `platform/arm64/platform_init.c:56-82`

### 4. Timer Support (timer.c/h)

Add millisecond time tracking:

```c
// Get current time in milliseconds
uint64_t timer_get_current_time_ms(void);
```

**Implementation approaches:**
- **x64**: TSC (Time Stamp Counter) calibrated against PIT
- **arm64**: Generic Timer counter (CNTPCT_EL0) divided by frequency

Capture start time in `timer_init()`, return elapsed milliseconds:

```c
static uint64_t g_timer_start = 0;

void timer_init(void) {
    // ... existing initialization ...
    g_timer_start = read_counter();  // Platform-specific
}

uint64_t timer_get_current_time_ms(void) {
    uint64_t now = read_counter();
    uint64_t elapsed = now - g_timer_start;
    // Convert to milliseconds based on frequency
    return (elapsed * 1000) / timer_frequency;
}
```

**Reference files:**
- `platform/x64/timer.c:243-252` (TSC-based)
- `platform/arm64/timer.c:137-149` (Generic Timer)

### 5. IRQ Dispatch Framework (interrupt.c/h)

Add IRQ handler registration table for device drivers:

```c
// In interrupt.h
void irq_register(uint32_t irq_num, void (*handler)(void*), void* context);
void irq_enable(uint32_t irq_num);
void irq_dispatch(uint32_t irq_num);

// In interrupt.c
#define MAX_IRQS 1024  // Or platform-appropriate size

typedef struct {
    void* context;
    void (*handler)(void* context);
} irq_entry_t;

static irq_entry_t g_irq_table[MAX_IRQS];

void irq_register(uint32_t irq_num, void (*handler)(void*), void* context) {
    g_irq_table[irq_num].handler = handler;
    g_irq_table[irq_num].context = context;
    // Configure interrupt controller routing
}

void irq_dispatch(uint32_t irq_num) {
    if (g_irq_table[irq_num].handler != NULL) {
        g_irq_table[irq_num].handler(g_irq_table[irq_num].context);
    }
}
```

Update exception/interrupt handler to call `irq_dispatch()` for device IRQs.

**Reference files:**
- `platform/x64/interrupt.c:99-105,235-249,252-282`
- `platform/arm64/interrupt.c:48-59,230-270`

### 6. PCI Support (pci.c/h, pci_scan.c)

PCI configuration space access is **architecture-specific**:

**x64**: I/O port-based (legacy PCI)
```c
// Use I/O ports 0xCF8 (address) and 0xCFC (data)
outl(PCI_CONFIG_ADDR, address);
return inl(PCI_CONFIG_DATA);
```

**ARM64**: Memory-mapped (ECAM)
```c
// Use ECAM base address 0x4010000000
uint64_t addr = ECAM_BASE | (bus << 20) | (device << 15) | (func << 12) | offset;
return *(volatile uint32_t*)addr;
```

PCI scanning logic is **portable** - copy from x64 or arm64.

**Reference files:**
- `platform/x64/pci.c` (I/O port implementation)
- `platform/x64/pci.h` (portable header)
- `platform/x64/pci_scan.c` (portable scanner)
- `platform/arm64/pci.c` (ECAM implementation)

### 7. VirtIO-RNG Driver (virtio_pci.c/h)

The VirtIO driver is **mostly portable**, with architecture-specific adjustments:

**Memory barriers:**
- x64: `mfence` instruction
- arm64: `dmb sy` instruction

**IRQ handling:**
- x64: Vector-based (LAPIC vectors 32+)
- arm64: IRQ number-based (GIC IRQs 32+)

**Portable components:**
- VirtIO capability parsing
- Virtqueue setup and management
- Device initialization sequence
- Request tracking array
- `kplatform_tick()` and `platform_submit()` implementations

Copy from x64 or arm64 and adjust:
1. Memory barrier instruction
2. IRQ registration (vector vs IRQ number)
3. Remove x64-specific includes (e.g., `io.h`)

**Reference files:**
- `platform/x64/virtio_pci.c:1-334` (complete driver)
- `platform/x64/virtio_pci.h` (device structures)
- `platform/arm64/virtio_pci.c` (ARM64 variant)

### 8. Build System (platform.mk)

Add new sources:

```makefile
PLATFORM_C_SRCS = uart.c platform_init.c interrupt.c timer.c \
                  pci.c pci_scan.c virtio_pci.c

QEMU_EXTRA_ARGS = -device virtio-rng-pci
```

**Reference files:**
- `platform/x64/platform.mk`
- `platform/arm64/platform.mk`

## Implementation Checklist

Use this checklist when porting to a new platform:

- [ ] Update `platform_impl.h`: Add `kernel_t* kernel` and `virtio_rng_t* virtio_rng`
- [ ] Remove `last_interrupt` field from platform_t
- [ ] Update `platform_init()`: Remove `interrupt_enable()` call
- [ ] Update `platform_wfi()`: Return time in milliseconds (not interrupt code)
- [ ] Add `timer_get_current_time_ms()` function to timer.c/h
- [ ] Add IRQ dispatch framework: `irq_register()`, `irq_enable()`, `irq_dispatch()`
- [ ] Implement PCI configuration space access (I/O ports or ECAM)
- [ ] Port PCI scanning logic (usually just copy pci_scan.c)
- [ ] Port VirtIO-RNG driver, adjust memory barriers and IRQ handling
- [ ] Add new sources to platform.mk
- [ ] Add `-device virtio-rng-pci` to QEMU_EXTRA_ARGS
- [ ] Test build and boot
- [ ] Verify VirtIO device detection in PCI scan

## Architecture-Specific Considerations

### Interrupt Controllers

Different platforms use different interrupt controllers:

| Platform | Interrupt Controller | Notes |
|----------|---------------------|-------|
| x64 | LAPIC + IOAPIC | Vector-based (0-255), MSI support |
| arm64 | GIC (GICv2/v3) | IRQ numbers, SPI starts at 32 |
| rv64 | PLIC | Priority-based, context routing |
| arm32 | GIC (GICv1/v2) | Similar to arm64 |

Key points:
- Implement `irq_register()` to configure routing to correct CPU
- Implement `irq_enable()` to unmask interrupts in controller
- Send EOI (End of Interrupt) after handling (LAPIC, GIC, PLIC-specific)

### Memory Barriers

Use appropriate memory barrier for architecture:

| Platform | Barrier Instruction |
|----------|-------------------|
| x64 | `mfence` |
| arm64 | `dmb sy` |
| rv64 | `fence` |
| arm32 | `dmb` |

### PCI Configuration Access

| Platform | Method | Base Address |
|----------|--------|--------------|
| x64 | I/O ports | 0xCF8 / 0xCFC |
| arm64 | ECAM MMIO | 0x4010000000 (QEMU virt) |
| rv64 | ECAM MMIO | Platform-specific |

Check your platform's device tree or documentation for PCI ECAM base address.

### Atomic Operations

The async runtime requires atomic operations for:
- Checking `irq_pending` flag in ISR and WFI
- Memory barriers around device MMIO

On x64, aligned loads/stores ≤ 8 bytes are atomic. On other platforms, use:
- `__sync_synchronize()` for full memory barrier
- Architecture-specific barriers as needed

## Testing

After porting, verify:

1. **Kernel boots** and reaches event loop
2. **PCI scan** finds VirtIO-RNG device
3. **Device initialization** succeeds (check QEMU output for errors)
4. **Interrupts work** (if not, check IRQ routing and EOI)
5. **User code** can submit RNG requests and receive callbacks

Use `printk()` liberally during debugging, but **never in ISRs** (not re-entrant).

## Common Pitfalls

1. **Enabling interrupts too early**: Must wait until event loop starts
2. **Missing memory barriers**: Can cause device state corruption
3. **Wrong PCI base address**: Check device tree or platform docs
4. **Incorrect IRQ routing**: Verify interrupt controller configuration
5. **Not sending EOI**: Causes interrupt controller to hang
6. **Calling printk in ISR**: Causes re-entrancy issues

## Additional Resources

- **Async Architecture**: `doc/async.md` - Full async runtime design
- **Interrupt Architecture**: `doc/interrupts.md` - Deferred processing model
- **VirtIO Integration**: `doc/virtio.md` - VirtIO device integration guide
- **x64 Interrupts**: `doc/interrupt-x64.md` - x64-specific interrupt details

## Example: Minimal Port Skeleton

For a new platform `foo`:

```
platform/foo/
├── platform_impl.h      # Add kernel*, virtio_rng* fields
├── platform_init.c      # Implement platform_wfi(), remove interrupt_enable()
├── interrupt.c/h        # Add irq_register/enable/dispatch
├── timer.c/h            # Add timer_get_current_time_ms()
├── pci.c/h              # Implement PCI config space access
├── pci_scan.c           # Copy from x64 or arm64
├── virtio_pci.c/h       # Copy and adapt from x64 or arm64
└── platform.mk          # Add new sources, QEMU args
```

Build and test:
```bash
make ARCH=foo
make run ARCH=foo
```

Look for "VirtIO-RNG initialized successfully" in output.

# VMOS Platform API Reference

## Overview

The **Platform API** (`kernel/platform.h`) defines the complete contract between the hardware-agnostic kernel and platform-specific implementations. Every platform (x64, arm32, arm64, rv32, rv64) must implement this interface to support VMOS.

**Purpose:**
- Abstract hardware differences (interrupt controllers, timers, PCI/MMIO access)
- Provide uniform interface for device drivers
- Enable multi-architecture support with shared kernel code

**Key Principle:** The kernel calls platform functions (downward dependency). The platform calls back via `kplatform_*` functions (completion notification).

## Platform Structure

Each platform defines a `platform_t` structure in `platform/*/platform_impl.h`:

```c
struct platform {
    // Platform-specific state
    // - Interrupt controller state
    // - Timer state
    // - VirtIO device structures
    // - IRQ table
    // - Memory regions
    // - FDT/ACPI data
};
```

**Example:** `platform/arm64/platform_impl.h:42-126`

Platform structure typically contains:
- Timer state (frequency, start time, callback)
- VirtIO device transports and drivers
- Interrupt controller state (IRQ table, IRQ ring)
- Memory regions discovered from FDT
- MMIO regions for devices
- Back-pointer to kernel

## Platform API Sections

The platform.h contract is organized into 9 sections:

1. **Memory Management** - Memory region discovery
2. **PCI** - PCI configuration space access
3. **MMIO** - Memory-mapped I/O with barriers
4. **Platform Lifecycle** - Init, WFI, submit, abort
5. **Debug** - Register and stack dumps
6. **Interrupt Control** - Global enable/disable
7. **UART** - Debug output
8. **IRQ Management** - Interrupt registration
9. **Device Discovery** - Hardware enumeration

---

## Section 1: Memory Management

### platform_mem_regions()

Get list of available (free) memory regions.

```c
kregions_t platform_mem_regions(platform_t *platform);
```

**Parameters:**
- `platform`: Platform state structure

**Returns:** `kregions_t` (linked list of `kregion_t`)

**When called:** After `platform_init()` completes

**Purpose:** Discover free RAM regions suitable for heap allocation (if implemented)

**Implementation notes:**
- Parse device tree (FDT) on ARM/RISC-V
- Parse E820 map on x86
- Return linked list of non-reserved memory regions

**Example structure:**
```c
typedef struct kregion {
    uintptr_t base;
    size_t size;
    struct kregion *next;
} kregion_t;

typedef kregion_t* kregions_t;
```

**Location:** `kernel/platform.h:22-25`

---

## Section 2: PCI Configuration Space

PCI configuration space access for device discovery and setup.

### Read Functions

```c
uint8_t platform_pci_config_read8(platform_t *platform, uint8_t bus,
                                  uint8_t slot, uint8_t func, uint8_t offset);
uint16_t platform_pci_config_read16(platform_t *platform, uint8_t bus,
                                    uint8_t slot, uint8_t func, uint8_t offset);
uint32_t platform_pci_config_read32(platform_t *platform, uint8_t bus,
                                    uint8_t slot, uint8_t func, uint8_t offset);
```

**Parameters:**
- `platform`: Platform state
- `bus`: PCI bus number (0-255)
- `slot`: PCI device slot (0-31)
- `func`: PCI function (0-7)
- `offset`: Register offset (0-255, aligned to access size)

**Returns:** Value at specified configuration space address

**Implementation methods:**
- **x64**: I/O ports 0xCF8 (address) / 0xCFC (data)
- **ARM64/RISC-V**: ECAM (Enhanced Configuration Access Mechanism) via MMIO
- **No PCI support**: Stub returning 0xFFFF

### Write Functions

```c
void platform_pci_config_write8(platform_t *platform, uint8_t bus,
                                uint8_t slot, uint8_t func, uint8_t offset,
                                uint8_t value);
void platform_pci_config_write16(platform_t *platform, uint8_t bus,
                                 uint8_t slot, uint8_t func, uint8_t offset,
                                 uint16_t value);
void platform_pci_config_write32(platform_t *platform, uint8_t bus,
                                 uint8_t slot, uint8_t func, uint8_t offset,
                                 uint32_t value);
```

**Use cases:**
- Enable bus mastering (command register)
- Configure BARs
- Set up MSI/MSI-X

### BAR Reading

```c
uint64_t platform_pci_read_bar(platform_t *platform, uint8_t bus,
                               uint8_t slot, uint8_t func, uint8_t bar_num);
```

**Parameters:**
- `bar_num`: BAR index (0-5)

**Returns:** Physical address mapped by BAR, or 0 if not present

**Implementation:** Read BAR, probe size, restore original value, calculate address

**Location:** `kernel/platform.h:28-56`

---

## Section 2A: MMIO Register Access

Memory-mapped I/O with appropriate memory barriers for weakly-ordered architectures.

### Read Functions

```c
uint8_t platform_mmio_read8(volatile uint8_t *addr);
uint16_t platform_mmio_read16(volatile uint16_t *addr);
uint32_t platform_mmio_read32(volatile uint32_t *addr);
uint64_t platform_mmio_read64(volatile uint64_t *addr);
```

**Purpose:** Read device registers with proper synchronization

**Memory barriers ensure:**
- MMIO operations complete before proceeding
- No speculative reads/writes to device registers
- Proper ordering in weakly-ordered memory models

### Write Functions

```c
void platform_mmio_write8(volatile uint8_t *addr, uint8_t val);
void platform_mmio_write16(volatile uint16_t *addr, uint16_t val);
void platform_mmio_write32(volatile uint32_t *addr, uint32_t val);
void platform_mmio_write64(volatile uint64_t *addr, uint64_t val);
```

### Memory Barrier

```c
void platform_mmio_barrier(void);
```

**Platform-specific implementations:**
- **ARM64**: `dsb sy` (Data Synchronization Barrier, full system)
- **ARM32**: `dmb sy` (Data Memory Barrier)
- **RISC-V**: `fence iorw, iorw`
- **x86**: No-op (strongly-ordered memory model)

**Implementation location:** `platform/*/mmio.c`, inline in `platform_impl.h`

**Example (ARM64):**
```c
static inline void platform_mmio_barrier(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}
```

**Location:** `kernel/platform.h:58-86`, `platform/arm64/platform_impl.h:175-191`

---

## Section 3: Platform Lifecycle

Core platform lifecycle functions.

### platform_init()

Initialize platform-specific features.

```c
void platform_init(platform_t *platform, void *fdt, void *kernel);
```

**Parameters:**
- `platform`: Platform state structure (zeroed before call)
- `fdt`: Pointer to Flattened Device Tree (NULL if not used)
- `kernel`: Pointer to kernel structure (platform can store as `platform->kernel`)

**Responsibilities:**
1. Parse device tree (FDT) or ACPI tables
2. Initialize UART for debug output
3. Set up interrupt controller (but don't enable interrupts yet)
4. Configure platform timer
5. Discover and enumerate devices (PCI scan or MMIO probe)
6. Initialize VirtIO drivers
7. Store kernel pointer for callbacks

**Important:** Do NOT enable interrupts in `platform_init()`. The kernel calls `platform_interrupt_enable()` after initialization completes.

**Implementation location:** `platform/*/platform_init.c`

### platform_wfi()

Wait for interrupt with timeout.

```c
ktime_t platform_wfi(platform_t *platform, ktime_t timeout_ns);
```

**Parameters:**
- `platform`: Platform state
- `timeout_ns`: Timeout in nanoseconds (`UINT64_MAX` = wait forever, `0` = return immediately)

**Returns:** Current time in nanoseconds after waking

**Behavior:**
1. Configure platform timer for timeout (if not `UINT64_MAX`)
2. Execute WFI/HLT instruction (wait for interrupt)
3. When interrupt arrives or timeout expires, wake up
4. Process interrupt (call registered handler)
5. Read current time and return

**Platform-specific WFI instructions:**
- **ARM64**: `wfi` (Wait For Interrupt)
- **ARM32**: `wfi`
- **RISC-V**: `wfi`
- **x86**: `hlt` (Halt)

**Implementation location:** `platform/*/platform_init.c`

### platform_submit()

Submit work and cancellations to platform.

```c
void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations);
```

**Parameters:**
- `platform`: Platform state
- `submissions`: Singly-linked list of work to submit (or NULL)
- `cancellations`: Singly-linked list of work to cancel (or NULL)

**Called by:** `kmain_tick()` during bulk submission

**Responsibilities:**
1. Iterate through submissions list via `work->next`
2. For each work item, inspect `work->op` to determine type
3. Submit to appropriate device driver (VirtIO RNG, block, network, etc.)
4. Best-effort attempt to cancel cancellations list

**Example:**
```c
void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations) {
    // Process submissions
    for (kwork_t *work = submissions; work != NULL; work = work->next) {
        switch (work->op) {
            case KWORK_OP_RNG_READ:
                krng_req_t *rng = CONTAINER_OF(work, krng_req_t, work);
                virtio_rng_submit(platform->virtio_rng_ptr, rng);
                break;
            case KWORK_OP_BLOCK_READ:
            case KWORK_OP_BLOCK_WRITE:
                kblk_req_t *blk = CONTAINER_OF(work, kblk_req_t, work);
                virtio_blk_submit(platform->virtio_blk_ptr, blk);
                break;
            // ... more cases
        }
    }

    // Process cancellations (best-effort)
    for (kwork_t *work = cancellations; work != NULL; work = work->next) {
        // Attempt to cancel work
    }
}
```

**Implementation location:** `platform/*/platform_virtio.c`

### platform_abort()

Abort system execution (shutdown/halt).

```c
noreturn void platform_abort(void);
```

**When called:** Fatal error or assertion failure

**Behavior:**
1. Print diagnostic information (optional)
2. Halt CPU or shutdown QEMU
3. Never return

**Platform-specific implementations:**
- **QEMU**: Write to QEMU exit device
- **Bare metal**: Infinite loop with interrupts disabled

**Location:** `kernel/platform.h:110-113`

---

## Section 3A: Platform Debug

Debug register and stack dumping.

### platform_dump_registers()

Dump platform registers to debug console.

```c
void platform_dump_registers(void);
```

**Prints:** Platform-specific register state
- **x64**: RIP, RSP, RAX, RBX, RCX, RDX, etc.
- **ARM64**: PC, SP, X0-X30, LR
- **RISC-V**: PC, SP, RA, T0-T6, A0-A7

### platform_dump_stack()

Dump stack contents to debug console.

```c
void platform_dump_stack(uint32_t bytes);
```

**Parameters:**
- `bytes`: Number of bytes to dump from current stack pointer

**Location:** `kernel/platform.h:115-125`

---

## Section 4: Interrupt Control

Global interrupt enable/disable.

### platform_interrupt_enable()

Enable interrupts globally.

```c
void platform_interrupt_enable(platform_t *platform);
```

**When called:** After `platform_init()` completes

**Platform-specific:**
- **ARM64**: `msr daifclr, #2` (clear I bit)
- **ARM32**: `cpsie i`
- **RISC-V**: `csrs mstatus, 0x8` (set MIE)
- **x86**: `sti`

### platform_interrupt_disable()

Disable interrupts globally.

```c
void platform_interrupt_disable(platform_t *platform);
```

**Platform-specific:**
- **ARM64**: `msr daifset, #2` (set I bit)
- **ARM32**: `cpsid i`
- **RISC-V**: `csrc mstatus, 0x8` (clear MIE)
- **x86**: `cli`

**Location:** `kernel/platform.h:127-135`

---

## Section 5: UART Debug Output

Debug console output (typically UART).

### platform_uart_puts()

Output null-terminated string.

```c
void platform_uart_puts(const char *str);
```

### platform_uart_putc()

Output single character.

```c
void platform_uart_putc(char c);
```

**Implementation:**
- Write directly to UART MMIO registers
- Spin-wait for transmit ready (not interrupt-driven)
- Used by `printk()` for kernel logging

**Platform-specific UART types:**
- **ARM64**: PL011
- **x64**: 16550 UART
- **RISC-V**: 16550 UART or SiFive UART

**Location:** `kernel/platform.h:137-145`

---

## Section 7: IRQ Management

Interrupt registration and enabling.

### platform_irq_register()

Register an interrupt handler.

```c
int platform_irq_register(platform_t *platform, uint32_t irq_num,
                          void (*handler)(void *), void *context);
```

**Parameters:**
- `platform`: Platform state
- `irq_num`: Platform-specific IRQ number
- `handler`: Function to call when interrupt fires
- `context`: Opaque context pointer passed to handler

**Returns:** 0 on success, negative on error

**Behavior:**
1. Store handler and context in IRQ table
2. Configure interrupt controller for this IRQ
3. Do NOT enable IRQ yet (caller must call `platform_irq_enable()`)

**Example:**
```c
void virtio_irq_handler(void *context) {
    virtio_rng_dev_t *dev = (virtio_rng_dev_t *)context;
    // Handle interrupt...
}

platform_irq_register(platform, irq_num, virtio_irq_handler, rng_dev);
platform_irq_enable(platform, irq_num);
```

### platform_irq_enable()

Enable (unmask) a specific IRQ.

```c
void platform_irq_enable(platform_t *platform, uint32_t irq_num);
```

**Platform-specific interrupt controllers:**
- **ARM64**: GICv2 or GICv3 (Generic Interrupt Controller)
- **ARM32**: GICv2
- **RISC-V**: PLIC (Platform-Level Interrupt Controller)
- **x64**: APIC (Advanced Programmable Interrupt Controller) or PIC

**Location:** `kernel/platform.h:147-163`

---

## Section 8: Work Submission

Process work queue changes and deferred interrupt work.

### platform_submit()

(Already documented in Section 3)

### platform_tick()

Platform tick - process deferred interrupt work.

```c
void platform_tick(platform_t *platform, kernel_t *k);
```

**Parameters:**
- `platform`: Platform state
- `k`: Kernel state

**When called:** During `kmain_tick()`, before callbacks run

**Purpose:** Process deferred interrupt work and move completed work to READY state

**Typical implementation:**
1. Process IRQ ring buffer (interrupts add entries, tick processes them)
2. For each completed work item, call `kplatform_complete_work(k, work, result)`
3. Clear processed IRQ ring entries

**Why deferred processing?**
- Minimize time spent in interrupt context
- `printk()` is not reentrant (can't call from IRQ handler)
- Complex work (virtqueue processing) should run outside IRQ context

**Example:**
```c
void platform_tick(platform_t *platform, kernel_t *k) {
    // Process IRQ ring
    while (kirq_ring_available(&platform->irq_ring) > 0) {
        kirq_entry_t entry = kirq_ring_consume(&platform->irq_ring);

        switch (entry.type) {
            case KIRQ_TYPE_VIRTIO_RNG:
                virtio_rng_process_completion(platform->virtio_rng_ptr, k);
                break;
            // ... more device types
        }
    }
}
```

### platform_net_buffer_release()

Release a network receive buffer back to the ring.

```c
void platform_net_buffer_release(platform_t *platform, void *req,
                                 size_t buffer_index);
```

**Parameters:**
- `platform`: Platform state
- `req`: Network receive request (`knet_recv_req_t*`)
- `buffer_index`: Which buffer to release (0 to `num_buffers-1`)

**When called:** User processed packet and wants to return buffer to device

**Purpose:** For standing network receive work, return buffer to virtqueue

**Location:** `kernel/platform.h:165-189`

---

## Section 9: Device Discovery

Platform-specific hardware enumeration.

### Platform MMIO Device Descriptor

```c
typedef struct {
    uint64_t mmio_base;   // MMIO base address
    uint32_t mmio_size;   // Size of MMIO region
    uint32_t irq_num;     // Platform IRQ number
    uint32_t device_id;   // VirtIO device ID (VIRTIO_ID_*)
    bool valid;           // Whether this device entry is valid
} platform_mmio_device_t;
```

### platform_discover_mmio_devices()

Discover MMIO devices.

```c
int platform_discover_mmio_devices(platform_t *platform,
                                   platform_mmio_device_t *devices,
                                   int max_devices);
```

**Parameters:**
- `platform`: Platform state
- `devices`: Output array to fill with discovered devices
- `max_devices`: Maximum number of devices to return

**Returns:** Number of valid devices found

**Discovery methods:**
- **ARM/RISC-V**: Parse FDT (Flattened Device Tree)
- **x64**: Probe hardcoded MMIO ranges (QEMU) or parse ACPI

**Example:**
```c
platform_mmio_device_t devices[8];
int num_devices = platform_discover_mmio_devices(platform, devices, 8);

for (int i = 0; i < num_devices; i++) {
    if (devices[i].device_id == VIRTIO_ID_RNG) {
        // Initialize RNG device at devices[i].mmio_base
    }
}
```

### platform_pci_setup_interrupts()

Configure PCI device interrupts.

```c
int platform_pci_setup_interrupts(platform_t *platform, uint8_t bus,
                                  uint8_t slot, uint8_t func, void *transport);
```

**Parameters:**
- `platform`: Platform state
- `bus`, `slot`, `func`: PCI device address
- `transport`: VirtIO PCI transport structure (for MSI-X configuration)

**Returns:** IRQ number or CPU vector to register (negative on error)

**Behavior:**
- **x64**: Configure MSI-X, disable INTx, return CPU vector number
- **ARM/RISC-V**: Read interrupt pin, calculate swizzled IRQ, return IRQ number

**IRQ swizzling (INTx):**
```
irq = base_irq + ((slot + int_pin - 1) % 4)
```

**Location:** `kernel/platform.h:191-226`

---

## Platform-Specific Request Fields

Each work request type has platform-specific fields embedded:

```c
typedef struct {
    kwork_t work;
    uint8_t *buffer;
    size_t length;
    size_t completed;
    krng_req_platform_t platform; // Platform-specific (64 bytes)
} krng_req_t;
```

**Platform types defined in `platform_impl.h`:**

```c
// ARM64 RNG request (platform/arm64/platform_impl.h:129-131)
typedef struct {
    uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;

// ARM64 Block request (platform/arm64/platform_impl.h:134-136)
typedef struct {
    uint16_t desc_idx; // VirtIO descriptor chain head index
} kblk_req_platform_t;

// ARM64 Network receive request (platform/arm64/platform_impl.h:142-146)
typedef struct {
    uint16_t desc_heads[KNET_MAX_BUFFERS]; // Persistent descriptors
    bool descriptors_allocated;
} knet_recv_req_platform_t;

// ARM64 Network send request (platform/arm64/platform_impl.h:149-151)
typedef struct {
    uint16_t desc_idx; // VirtIO descriptor chain head index
} knet_send_req_platform_t;
```

**Why platform-specific fields?**
- Avoid dynamic allocation
- Store device-specific state (descriptor indices, DMA addresses)
- Opaque to kernel and user code

---

## Porting Guide

### Adding a New Platform

To add a new platform (e.g., `riscv32`):

#### 1. Create Platform Directory

```bash
mkdir -p platform/rv32
```

#### 2. Create `platform.mk`

Define build configuration:

```make
TARGET = riscv32-none-elf
QEMU = qemu-system-riscv32
QEMU_MACHINE = virt
QEMU_CPU = rv32

PLATFORM_C_SRCS = \
    platform/rv32/platform_init.c \
    platform/rv32/platform_virtio.c \
    platform/rv32/interrupt.c \
    platform/rv32/timer.c \
    platform/rv32/mmio.c

PLATFORM_S_SRCS = \
    platform/rv32/boot.S

PLATFORM_SHARED_SRCS = \
    kernel/kmain.c \
    kernel/kernel.c \
    # ... more shared sources

PLATFORM_CFLAGS = -march=rv32ima -mabi=ilp32
PLATFORM_LDFLAGS = -T linker/rv32.ld
```

#### 3. Create `platform_impl.h`

Define `platform_t` structure and platform-specific types:

```c
#pragma once

#include <stdint.h>
#include "virtio/virtio_rng.h"
// ... more includes

struct platform {
    // Timer state
    uint64_t timer_freq_hz;
    uint64_t timer_start;

    // VirtIO devices
    virtio_rng_dev_t virtio_rng;
    // ... more devices

    // Interrupt controller state
    irq_entry_t irq_table[MAX_IRQS];
    kirq_ring_t irq_ring;

    // Kernel back-pointer
    void *kernel;

    // Memory regions
    kregion_t mem_regions[KCONFIG_MAX_MEM_REGIONS];
    int num_mem_regions;
};

// Request platform-specific fields
typedef struct {
    uint16_t desc_idx;
} krng_req_platform_t;

// MMIO barrier (RISC-V fence)
static inline void platform_mmio_barrier(void) {
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

// ... more inline functions
```

#### 4. Implement `boot.S`

Assembly entry point:

```asm
.section .text.boot
.globl _start
_start:
    # Set up stack
    la sp, stack_top

    # Call kmain with device tree pointer (a0 contains FDT address)
    mv a0, a1  # FDT passed in a1 on RISC-V
    call kmain

    # Halt on return
1:  wfi
    j 1b

.section .bss
.align 16
stack_bottom:
    .space 65536
stack_top:
```

#### 5. Implement `platform_init.c`

Platform initialization and WFI:

```c
#include "kernel.h"
#include "platform.h"
#include "printk.h"

void platform_init(platform_t *platform, void *fdt, void *kernel) {
    platform->kernel = kernel;

    // 1. Parse device tree (FDT)
    parse_fdt(platform, fdt);

    // 2. Initialize UART
    platform_uart_init(platform);

    // 3. Initialize interrupt controller (PLIC)
    plic_init(platform);

    // 4. Initialize timer
    timer_init(platform);

    // 5. Discover and initialize VirtIO devices
    discover_virtio_devices(platform);

    KLOG("platform_init complete");
}

ktime_t platform_wfi(platform_t *platform, ktime_t timeout_ns) {
    // Configure timer for timeout
    if (timeout_ns != UINT64_MAX) {
        timer_set_timeout(platform, timeout_ns);
    }

    // Wait for interrupt
    __asm__ volatile("wfi");

    // Read current time
    return timer_get_time_ns(platform);
}
```

#### 6. Implement `platform_virtio.c`

Device discovery and work submission:

```c
#include "kernel.h"
#include "platform.h"
#include "virtio/virtio_rng.h"

void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations) {
    // Process submissions
    for (kwork_t *work = submissions; work != NULL; work = work->next) {
        switch (work->op) {
            case KWORK_OP_RNG_READ:
                krng_req_t *rng = CONTAINER_OF(work, krng_req_t, work);
                virtio_rng_submit(&platform->virtio_rng, rng);
                break;
            // ... more cases
        }
    }

    // Best-effort cancellation
    // ...
}

void platform_tick(platform_t *platform, kernel_t *k) {
    // Process IRQ ring
    while (kirq_ring_available(&platform->irq_ring) > 0) {
        kirq_entry_t entry = kirq_ring_consume(&platform->irq_ring);
        // Process deferred interrupt work
    }
}
```

#### 7. Implement `interrupt.c`

Interrupt controller setup and IRQ handling:

```c
void plic_init(platform_t *platform) {
    // Initialize PLIC
    // Set priorities, thresholds, etc.
}

int platform_irq_register(platform_t *platform, uint32_t irq_num,
                          void (*handler)(void *), void *context) {
    platform->irq_table[irq_num].handler = handler;
    platform->irq_table[irq_num].context = context;
    return 0;
}

void platform_irq_enable(platform_t *platform, uint32_t irq_num) {
    // Enable IRQ in PLIC
}

// Interrupt entry point (called from trap handler)
void handle_irq(platform_t *platform, uint32_t irq_num) {
    irq_entry_t *entry = &platform->irq_table[irq_num];
    if (entry->handler) {
        entry->handler(entry->context);
    }
}
```

#### 8. Implement `timer.c`

Platform timer configuration:

```c
void timer_init(platform_t *platform) {
    // Read timer frequency
    // Set up timer registers
}

void timer_set_timeout(platform_t *platform, ktime_t timeout_ns) {
    // Configure timer to fire after timeout_ns
}

ktime_t timer_get_time_ns(platform_t *platform) {
    // Read current time in nanoseconds
}
```

#### 9. Implement `mmio.c`

MMIO register access:

```c
uint8_t platform_mmio_read8(volatile uint8_t *addr) {
    uint8_t val = *addr;
    platform_mmio_barrier();
    return val;
}

void platform_mmio_write8(volatile uint8_t *addr, uint8_t val) {
    *addr = val;
    platform_mmio_barrier();
}

// ... more MMIO functions
```

#### 10. Create Linker Script

`linker/rv32.ld`:

```ld
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS {
    . = 0x80000000;

    .text : {
        *(.text.boot)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
    }
}
```

#### 11. Test

```bash
make PLATFORM=rv32
make run PLATFORM=rv32
make test PLATFORM=rv32
```

---

## Platform-Specific Considerations

### ARM64

**Interrupt Controller:** GICv2 or GICv3 (Generic Interrupt Controller)
**Timer:** Generic Timer (CNTPCT_EL0, CNTFRQ_EL0)
**PCI:** ECAM via MMIO
**Memory Barriers:** `dsb sy` (Data Synchronization Barrier)

### ARM32

**Interrupt Controller:** GICv2
**Timer:** Generic Timer
**PCI:** ECAM via MMIO
**Memory Barriers:** `dmb sy` (Data Memory Barrier)

### RISC-V (rv64/rv32)

**Interrupt Controller:** PLIC (Platform-Level Interrupt Controller)
**Timer:** CLINT (Core Local Interruptor) or SBI timer
**PCI:** ECAM via MMIO
**Memory Barriers:** `fence iorw, iorw`

### x86-64

**Interrupt Controller:** APIC (Advanced Programmable Interrupt Controller)
**Timer:** LAPIC timer or HPET
**PCI:** I/O ports (0xCF8/0xCFC) or ECAM
**Memory Barriers:** Not needed (strongly-ordered memory model)
**MSI-X:** Fully supported for PCI devices

---

## Best Practices

### 1. Minimize Work in Interrupt Context

```c
// GOOD - defer work to platform_tick
void irq_handler(void *context) {
    // Add to IRQ ring
    kirq_ring_produce(&platform->irq_ring, entry);
}

// BAD - heavy processing in IRQ
void irq_handler(void *context) {
    process_virtqueue(); // Too much work!
    printk("IRQ!"); // NOT REENTRANT!
}
```

### 2. Use IRQ Ring Buffer Pattern

```c
// IRQ handler: minimal work
void virtio_irq(void *context) {
    kirq_entry_t entry = { .type = KIRQ_TYPE_VIRTIO_RNG };
    kirq_ring_produce(&platform->irq_ring, entry);
}

// platform_tick: deferred processing
void platform_tick(platform_t *platform, kernel_t *k) {
    while (kirq_ring_available(&platform->irq_ring) > 0) {
        kirq_entry_t entry = kirq_ring_consume(&platform->irq_ring);
        virtio_rng_process_completion(&platform->virtio_rng, k);
    }
}
```

### 3. Store Kernel Pointer in Platform

```c
void platform_init(platform_t *platform, void *fdt, void *kernel) {
    platform->kernel = kernel; // Store for callbacks
}

void completion_handler() {
    kernel_t *k = (kernel_t *)platform->kernel;
    kplatform_complete_work(k, work, KERR_OK);
}
```

### 4. Enable Interrupts After Init

```c
// kernel/kmain.c:135-140
void kmain_init(kernel_t *k, void *fdt) {
    memset(k, 0, sizeof(kernel_t));
    platform_init(&k->platform, fdt, k);
    k->current_time_ns = platform_wfi(&k->platform, 0);
    platform_interrupt_enable(&k->platform); // LAST!
}
```

---

## Summary

The Platform API provides:

✅ **Hardware abstraction** - Uniform interface across 5 architectures
✅ **Memory management** - Region discovery via FDT/E820
✅ **PCI support** - Configuration space access, BAR reading, interrupt setup
✅ **MMIO access** - Memory-mapped I/O with proper barriers
✅ **Interrupt handling** - Registration, enabling, deferred processing
✅ **Timer support** - Timeout-based WFI, monotonic time
✅ **Device discovery** - Platform-specific enumeration
✅ **Debug output** - UART for kernel logging

For more details:
- **Architecture**: See [architecture.md](architecture.md)
- **Async work**: See [async-work.md](async-work.md)
- **VirtIO drivers**: See [virtio.md](virtio.md)

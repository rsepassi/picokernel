# Interrupt Architecture

## Overview

vmos uses a **deferred interrupt processing** model where interrupt handlers do
minimal work and defer heavy processing to the main event loop (`ktick()`).
This document describes the interrupt handling architecture and integration
with the async work queue system.

## Core Principle: Minimal ISRs

**Interrupt Service Routines (ISRs) must be MINIMAL.** Heavy work is deferred
to `ktick()`.

### Why ISRs Must Be Minimal

1. **Re-entrancy hazards**: ISRs can interrupt any code, including itself or other ISRs
2. **No locking primitives**: vmos is single-threaded cooperative, but ISRs introduce preemption
3. **Stack depth**: Nested interrupts can overflow limited stack space
4. **Latency**: Long ISRs delay other interrupts and introduce jitter

### What ISRs Are Allowed To Do

✅ **SAFE in ISRs:**
- Read/write device MMIO registers (acknowledge interrupts, read status)
- Set flags (`volatile` variables)
- Write to lock-free ring buffers (atomics)
- Call LAPIC EOI (End Of Interrupt)

❌ **FORBIDDEN in ISRs:**
- Call `printk()` or any non-reentrant function
- Manipulate kernel work queues (`kplatform_complete_work()`, etc.)
- Allocate memory
- Acquire locks (we don't have them, but don't introduce them!)
- Perform complex logic or loops

## Deferred Processing Architecture

### Two-Phase Interrupt Handling

```
┌─────────────┐
│  Hardware   │ IRQ fires
│  Interrupt  │
└──────┬──────┘
       │
       ▼
┌─────────────────────────────────────────┐
│ Phase 1: ISR (Interrupt Context)        │
│ ────────────────────────────────────    │
│ 1. Acknowledge hardware interrupt       │
│ 2. Read device status (if needed)       │
│ 3. Set pending flag/enqueue to ring     │
│ 4. Send LAPIC EOI                       │
│ 5. Return (IRET)                        │
└──────┬──────────────────────────────────┘
       │
       ▼ (HLT wakes, time passes)
       │
┌──────▼──────────────────────────────────┐
│ Phase 2: ktick (Process Context)        │
│ ────────────────────────────────────    │
│ 1. Check pending flags/ring buffer      │
│ 2. Process used buffers (VirtIO, etc.)  │
│ 3. Call kplatform_complete_work()       │
│ 4. Manipulate work queues               │
│ 5. Run callbacks                        │
└─────────────────────────────────────────┘
```

### Integration With Event Loop

```c
void main(void* fdt) {
    kernel_t k;
    kinit(&k, fdt);
    kusermain(&k);

    uint64_t current_time = 0;
    while (1) {
        ktick(&k, current_time);  // ← Deferred IRQ work happens HERE
        current_time = platform_wfi(&k.platform, knext_delay(&k));
    }
}
```

## Implementation: SPSC Ring Buffer (Scalable, Multi-Device)

### Ring Buffer Structure

```c
#define IRQ_RING_SIZE 256

typedef struct {
    void* items[IRQ_RING_SIZE];
    volatile uint32_t write_pos;  // ISR writes here
    volatile uint32_t read_pos;   // ktick reads here
} irq_ring_t;

// In platform_t
typedef struct {
    irq_ring_t irq_ring;
    // ... other platform state ...
} platform_t;
```

### ISR (Lock-Free Enqueue)

```c
static void device_irq_handler(void* context) {
    device_t* dev = (device_t*)context;
    kernel_t* k = dev->kernel;

    // 1. Acknowledge hardware interrupt
    device_ack_interrupt(dev);

    // 2. Enqueue device pointer (lock-free)
    uint32_t pos = k->platform.irq_ring.write_pos;
    k->platform.irq_ring.items[pos % IRQ_RING_SIZE] = dev;

    // 3. Memory barrier before updating write pointer
    __sync_synchronize();

    // 4. Advance write pointer (atomic on x86)
    k->platform.irq_ring.write_pos = pos + 1;

    // 5. LAPIC EOI sent by irq_dispatch()
}
```

### Deferred Processing (ktick)

```c
void ktick(kernel_t* k, uint64_t current_time) {
    // ... timer expiration ...

    // Process all pending device IRQs
    irq_ring_t* ring = &k->platform.irq_ring;

    while (ring->read_pos != ring->write_pos) {
        // Get device from ring
        device_t* dev = ring->items[ring->read_pos % IRQ_RING_SIZE];

        // Process device-specific completion
        process_device_completion(dev, k);

        // Advance read pointer
        ring->read_pos++;
    }

    // ... run callbacks, submit work ...
}
```

**Ring Buffer Overflow:**
If `write_pos - read_pos >= IRQ_RING_SIZE`, the ring is full. ISR drops the
interrupt and sets an overflow flag.

**Pros:**
- Scales to unlimited devices
- Lock-free, cache-friendly
- Preserves device identity

**Cons:**
- More complex than flags
- Requires careful memory barrier usage

## Platform-Specific Implementation

### x86-64 IRQ Routing

```c
// platform/x64/interrupt.c

static irq_entry_t g_irq_table[MAX_IRQ_VECTORS];

void irq_register(uint8_t vector, void (*handler)(void*), void* context) {
    g_irq_table[vector].handler = handler;
    g_irq_table[vector].context = context;
}

void irq_dispatch(uint8_t vector) {
    if (g_irq_table[vector].handler != NULL) {
        g_irq_table[vector].handler(g_irq_table[vector].context);
    }

    // Send EOI to LAPIC
    lapic_send_eoi();
}
```

### ISR Assembly Stubs

```asm
; platform/x64/isr.S

isr_stub_42:
    push 42             ; Vector number
    jmp isr_common

isr_common:
    ; Save all registers
    push rax
    push rcx
    ; ... all other registers ...

    mov rdi, [rsp + 120]  ; Vector from stack
    call interrupt_handler

    ; Restore all registers
    pop rcx
    pop rax
    ; ... all other registers ...

    add rsp, 8          ; Remove vector
    iretq               ; Return from interrupt
```

## Memory Ordering and Atomicity

### x86-64 Guarantees

On x86-64:
- Aligned loads/stores are atomic for sizes ≤ 8 bytes
- `MOV` to memory has release semantics
- `MOV` from memory has acquire semantics

For other architectures, use explicit barriers:

```c
// Write (ISR)
ring->items[pos] = dev;
__sync_synchronize();  // Full memory barrier
ring->write_pos = pos + 1;

// Read (ktick)
uint32_t pos = ring->write_pos;
__sync_synchronize();  // Full memory barrier
device_t* dev = ring->items[pos];
```

## Debugging Interrupt Issues

### Common Problems

1. **GPF in ISR**: Usually from calling non-reentrant function (printk)
2. **Lost interrupts**: IRQ not unmasked in PIC/IOAPIC
3. **Spurious completions**: Missing memory barriers
4. **Hung system**: ISR doesn't send EOI

### Debug Strategy

1. **Remove all printk from ISRs** - Use flags instead
2. **Add global counter**: Increment in ISR, check in ktick
3. **Check IRQ masks**: Verify PIC/IOAPIC configuration
4. **Memory barriers**: Add barriers on non-x86 or when in doubt

### Simple Debug Pattern

```c
// In ISR
static volatile uint32_t g_irq_count = 0;
void device_irq_handler(void* context) {
    g_irq_count++;  // Atomic on x86
    // ... minimal work ...
}

// In ktick (or user code)
void check_irqs(void) {
    static uint32_t last_count = 0;
    uint32_t current = g_irq_count;
    if (current != last_count) {
        printk("IRQs fired: ");
        printk_dec(current);
        printk("\n");
        last_count = current;
    }
}
```

## Temporary Flag-Based Implementation

For **immediate debugging and single-device scenarios**, use a simple flag:

1. Add `volatile uint8_t irq_pending` to device struct
2. ISR: acknowledge HW interrupt, set `irq_pending = 1`, return
3. ktick: check flag, if set clear it and process completions
4. Move ALL printk and queue manipulation to ktick

This gets you working **immediately** with minimal changes. Migrate to ring buffer when adding more devices.

## Future Work

- **IOAPIC support**: Modern interrupt routing (vs legacy PIC)
- **MSI/MSI-X**: Message-signaled interrupts for PCIe
- **Per-CPU IRQ affinity**: Route interrupts to specific cores
- **IRQ coalescing**: Batch completions to reduce interrupt rate

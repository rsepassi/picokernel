# x64 Interrupt System

## Current State

The x64 interrupt machinery is **functionally correct** with deferred processing, but **PCI device interrupts do not fire** because we're using legacy 8259 PIC instead of IOAPIC.

### What Works

- ✅ Boot sequence (32-bit → long mode → 64-bit)
- ✅ IDT initialization (256 entries)
- ✅ Exception handlers (vectors 0-31)
- ✅ LAPIC timer interrupts (vector 32)
- ✅ Two-phase deferred interrupt processing
- ✅ VirtIO-RNG device communication (polling mode)
- ✅ Stack alignment (x86-64 ABI compliant)

### What Doesn't Work

- ❌ VirtIO-PCI interrupts (IRQ 10 via PIC - never fires)
- ❌ Any modern PCI device interrupts

## Boot Sequence

1. **PVH Entry** (boot.S): CPU starts in 32-bit protected mode
2. **Page Tables**: Identity map 0-4MB, map MMIO region 0xFE000000-0xFF000000
3. **Long Mode**: Enable PAE, set EFER.LME, enable paging
4. **64-bit Mode**: Far jump to `long_mode_start`
5. **Stack Setup**: 16-byte aligned stack, clear BSS
6. **Call main()**: Enter C code with interrupts disabled

## Interrupt Initialization

```
platform_init() → interrupt_init() → timer_init()
                                  ↓
                    IDT loaded, interrupts still disabled
                                  ↓
              (device enumeration happens with CLI)
                                  ↓
                  kmain() → interrupt_enable() (STI)
```

**Key principle:** Interrupts stay disabled (CLI) until entering the event loop to avoid spurious interrupts during device enumeration.

## ISR Implementation

Three ISR types in isr.S:

### Exception Handlers (0-31)
- Push error code (or dummy 0) and vector number
- Save 15 registers (120 bytes)
- **Align stack:** `sub $8, %rsp` (Bug #2 - FIXED)
- Load vector from **offset 136** (Bug #1 - FIXED, was 120)
- Call `interrupt_handler()`
- No EOI for exceptions (fatal)

### Timer Handler (32)
- Save 15 registers (120 bytes)
- **Align stack:** `sub $8, %rsp` (Bug #3 - FIXED)
- Call `interrupt_handler(32)`
- `lapic_timer_handler()` sends LAPIC EOI

### IRQ Handlers (33-47)
- Save 15 registers, align stack
- Call `interrupt_handler(vector)`
- `irq_dispatch()` sends LAPIC EOI

## Deferred Processing Pattern

```
1. ISR (minimal):
   - Read ISR status (VirtIO)
   - Set irq_pending flag
   - Return (EOI sent by dispatch)

2. HLT wakes → ktick()

3. kplatform_tick():
   - Check irq_pending
   - Process used ring
   - Call kplatform_complete_work()

4. ktick() runs callbacks
```

This matches the design doc perfectly.

## Root Cause: Legacy PIC vs IOAPIC

### The Problem

**VirtIO-PCI devices route interrupts through IOAPIC, NOT the legacy 8259 PIC.**

- Legacy PIC (8259): Handles ISA IRQs 0-15 only
- IOAPIC: Handles all PCI interrupts (IRQ 16+)
- Modern systems (QEMU) route PCI devices through IOAPIC
- We only initialized PIC, never IOAPIC
- Result: Device completes work (`used.idx=1`) but interrupt never arrives

### Evidence

```
[DEBUG] IRQ count=0, used.idx=1, last_used=0, pending=0
```

- Device works (used.idx incremented)
- Zero interrupts fired (count=0)
- PIC is correctly unmasked (verified)
- PCI interrupts enabled (command register bit 10 clear)
- IRQ handler registered (vector 42)

**Conclusion:** IOAPIC is routing the interrupt, but we never initialized IOAPIC, so it goes nowhere.

## Recommendation: Switch to IOAPIC

### Remove All Legacy PIC Code

The legacy 8259 PIC code should be **completely removed**:

- `pic_init()` in interrupt.c
- `pic_unmask_irq()` in interrupt.c
- `irq_enable()` PIC masking logic
- All `inb(0x21)` / `outb(0xA1)` port I/O

### Implement Modern IOAPIC

Modern x64 systems use **LAPIC + IOAPIC**:

1. **LAPIC** (already working):
   - Per-CPU local interrupts
   - Timer (vector 32)
   - Receives interrupts from IOAPIC

2. **IOAPIC** (needs implementation):
   - Memory-mapped at 0xFEC00000 (default)
   - Routes PCI interrupts to LAPIC
   - Programmable redirection table (24 entries typical)
   - Supports edge/level triggering, active high/low

### Implementation Steps

1. **Parse MADT** (ACPI table) to find IOAPIC base address
2. **Initialize IOAPIC**:
   - Map MMIO region (already mapped in boot.S)
   - Read version/max entries
   - Clear all redirection entries
3. **Route PCI interrupts**:
   - Read PCI interrupt pin/line
   - Program IOAPIC redirection entry
   - Set destination LAPIC, vector, trigger mode
4. **Remove PIC masking** - IOAPIC handles everything

### Benefits

- ✅ PCI device interrupts work
- ✅ Supports >15 IRQs
- ✅ Per-interrupt configuration (edge/level, polarity)
- ✅ Modern, standard approach
- ✅ Simpler than maintaining dual PIC/IOAPIC code

## Current Workaround

VirtIO-RNG works in **polling mode** (checking used ring on timer wakeups). This is NOT the intended design and should be removed once IOAPIC is implemented.

## Next Steps

1. Implement IOAPIC initialization and routing
2. Remove all legacy PIC code
3. Test VirtIO-PCI interrupts
4. Verify deferred processing with real interrupts
5. Remove polling workaround

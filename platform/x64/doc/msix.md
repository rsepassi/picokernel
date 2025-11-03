# x64 MSI-X Status

## Current State

MSI-X interrupts are **partially working**:
- **virtio-blk**: Working (interrupts firing)
- **virtio-net**: Working (interrupts firing)
- **virtio-rng**: Failing (device initialization fails)

Debug output shows MSI-X interrupts are being delivered (`[DEBUG] MSI-X
interrupts: 2`), confirming LAPIC and interrupt path work correctly.

## How MSI-X Works on x64

### 1. MSI-X Table Structure

Each MSI-X vector has a 16-byte entry in the MSI-X table (lives in a PCI BAR):
```c
struct msix_table_entry {
    uint32_t msg_addr_low;   // 0xFEE00000 | (apic_id << 12)
    uint32_t msg_addr_high;  // 0x00000000 (for x64)
    uint32_t msg_data;       // Interrupt vector number (33-47)
    uint32_t vector_control; // Bit 0 = mask (must be 0)
};
```

### 2. MSI-X Configuration Sequence

1. **PCI Config Space**: Locate MSI-X capability (ID 0x11)
   - Read Table Offset/BIR to find which BAR and offset
   - Read PBA Offset/BIR (Pending Bit Array, not currently used)

2. **Program MSI-X Table**: For each vector used by device
   - Map the BAR containing the table
   - Write address (always 0xFEE00000 | (apic_id << 12) on x64)
   - Write data (interrupt vector number)
   - Clear mask bit (vector_control = 0)

3. **Enable MSI-X**: In PCI config space
   - Set bit 15 (MSI-X Enable) in Message Control register
   - Clear bit 14 (Function Mask)
   - Set bit 10 (INT_DISABLE) in Command register to disable legacy INTx

4. **Configure VirtIO Device**: Write MSI-X vector numbers to device
   - Config vector: 0xFFFF (no config interrupts)
   - Queue vectors: 0 for single-queue devices, 0/1 for multi-queue

### 3. Interrupt Delivery Path

When device needs to signal completion:
1. Device writes `msg_data` to `msg_addr_low` (memory write transaction)
2. LAPIC receives write at 0xFEE00000 + offset
3. LAPIC triggers interrupt for vector number in `msg_data`
4. CPU jumps to IDT entry for that vector
5. IRQ handler calls device-specific callback

No IOAPIC involvement for MSI-X (direct device→LAPIC path).

## Remaining Issue: BAR Overlap

### Problem

virtio-rng device has overlapping BARs:
```
[VIRTIO_PCI] COMMON_CFG in BAR4 at offset 0x00000000 (BAR base=0x00000000c0000000)
[MSI-X] MSI-X table in BAR1 at offset 0x00000000 (BAR base=0x00000000c0000000)
```

Both BAR1 and BAR4 map to **0xC0000000**. When MSI-X table is written, it corrupts VirtIO common_cfg registers. This causes device status register to read back as 0x00 after writing FEATURES_OK, failing initialization.

### Root Cause

Modern VirtIO PCI devices use **64-bit BARs**:
- BAR0+BAR1 form a 64-bit BAR (MSI-X table)
- BAR4+BAR5 form a 64-bit BAR (VirtIO capabilities)

When reading BAR1 independently, it returns the same base address as BAR4 (QEMU bug or misinterpretation of BAR type bits), causing overlap.

The `allocate_pci_bars()` function may not properly:
1. Detect 64-bit BARs (bits [2:1] = 0b10)
2. Skip upper half of 64-bit BAR when iterating
3. Validate that BARs don't overlap after assignment

### What's Needed

1. **Debug BAR allocation**: Add logging to see raw BAR values and how they're interpreted
2. **Fix 64-bit BAR handling**: Properly detect and skip upper 32-bit half
3. **Detect overlaps**: Check if any two BARs map to overlapping address ranges
4. **Correct assignments**: Either trust QEMU's assignments or manually reassign non-overlapping addresses

## Network MAC Address Issue

virtio-net reports MAC as `00:00:00:00:00:00`. The device config space read for MAC address may be:
1. Reading from wrong offset in BAR
2. Using uninitialized device_cfg pointer (since DEVICE_CFG is in BAR4, which may be corrupted)
3. Reading before device is properly initialized

This is likely a side effect of the BAR overlap issue.

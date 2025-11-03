# x64 MSI-X Status

## Current State

MSI-X interrupts are **fully working** for all VirtIO devices:
- **virtio-rng**: Working (interrupts firing)
- **virtio-blk**: Working (interrupts firing)
- **virtio-net**: Working (interrupts firing)

All devices initialize successfully and handle interrupts via the MSI-X mechanism.

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

## 64-bit BAR Handling

Modern VirtIO PCI devices use **64-bit BARs**:
- BAR0+BAR1 form a single 64-bit BAR (typically for MSI-X table)
- BAR4+BAR5 form a single 64-bit BAR (typically for VirtIO capabilities)

When a BAR has bits [2:1] = 0b10, it indicates a 64-bit BAR where:
- The current BAR register contains the lower 32 bits
- The next BAR register contains the upper 32 bits

The PCI BAR reading code properly detects 64-bit BARs and treats the upper half (odd-numbered BARs like 1, 3, 5) as part of the previous 64-bit BAR rather than independent BARs. This prevents address overlaps and ensures MSI-X tables and VirtIO configuration spaces remain properly separated.

## Vector Assignments

The kernel uses the following interrupt vector assignments:
- Vector 33: virtio-rng (queue interrupts)
- Vector 34: virtio-blk (queue interrupts)
- Vector 35: virtio-net (RX and TX queue interrupts)
- Config vectors: 0xFFFF (disabled - we don't use configuration change interrupts)

Each device is configured to deliver interrupts directly to LAPIC ID 0 (the boot processor).

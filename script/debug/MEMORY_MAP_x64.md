# x64 Memory Map Analysis

## Physical Memory Layout (128 MiB total)

### Low Memory (0x00000000 - 0x000FFFFF) - 1 MiB
```
0x00000000 - 0x00007FFF:  BIOS Data Area (32 KiB)
                          - Real mode IVT (0x0000-0x03FF)
                          - BIOS data (0x0400-0x04FF)
                          - Free/Reserved (0x0500-0x7FFF)

0x00009000 - 0x0000DFFF:  Page Tables (20 KiB)
                          - PML4 at 0x9000 (4 KiB)
                          - PDPT at 0xA000 (4 KiB)
                          - PD0 at 0xB000 (4 KiB, maps 0-1GB)
                          - PD3 at 0xC000 (4 KiB, maps 3-4GB MMIO)
                          - 0xD000-0xDFFF unused

0x000E0000 - 0x000FFFFF:  Extended BIOS Area
                          - Video BIOS
                          - Option ROMs
```

### Kernel Memory (0x00200000 - 0x0023DFFF) - ~240 KiB
```
0x00200000 - 0x00200013:  .note.Xen (20 bytes)
0x00200020 - 0x0020C2D3:  .text (49,844 bytes = ~49 KiB)
                          - Includes .text.boot
0x0020D000 - 0x0020EC4F:  .rodata (7,248 bytes = ~7 KiB)
                          - Page-aligned
                          - Contains GDT
0x0020F000 - 0x0023DBFF:  .bss (191,488 bytes = ~187 KiB)
                          - Page-aligned
                          - Contains:
                            - IDT at 0x133040 (4,096 bytes)
                            - g_kernel at 0x224000 (77,824 bytes = ~76 KiB)
                            - g_user at 0x237000 (14,848 bytes = ~14.5 KiB)
                            - stack_bottom at 0x2130F0
                            - stack_top at 0x2230F0 (64 KiB stack)

0x0023DC00:               _end (kernel end marker)
```

### ACPI Tables (discovered at runtime)
```
0x0010E000 - 0x0010F000:  RSDP + ACPI tables (~4 KiB estimated)
                          - RSDP at 0x10E000
                          - RSDT/XSDT
                          - MADT at 0x10E368
```

### Free RAM
```
0x0023E000 - 0x07FFFFFF:  Free memory (~126 MiB)
                          - Available for dynamic allocation
                          - No allocator implemented yet
```

## Memory-Mapped I/O Regions (Virtual & Physical)

### PCI Configuration Space
```
0xC0000000 - 0xCFFFFFFF:  PCI BAR MMIO region (256 MiB)
                          - Mapped by boot.S (128 x 2MB huge pages)
                          - Cache disabled (PCD + PWT flags)
                          - pci_next_bar_addr initialized to 0xC0000000
                          - QEMU microvm: BARs auto-assigned, this is manual fallback
```

### High MMIO Region
```
0xFE000000 - 0xFEFFFFFF:  High MMIO (16 MiB)
                          - Mapped by boot.S (8 x 2MB huge pages)
                          - Cache disabled

0xFEB00000 - 0xFEB0FFFF:  VirtIO MMIO device region (64 KiB)
                          - Device 0: 0xFEB02A00 (VirtIO-Net)
                          - Device 1: 0xFEB02C00 (VirtIO-Block)
                          - Device 2: 0xFEB02E00 (VirtIO-RNG)
                          - 0x200 (512 bytes) stride per device

0xFEC00000 - 0xFEC00FFF:  IOAPIC #0 (4 KiB)
0xFEC10000 - 0xFEC10FFF:  IOAPIC #1 (4 KiB)

0xFEE00000 - 0xFEE00FFF:  Local APIC (4 KiB)
```

## Detailed Kernel Structure Layout

### g_kernel (BSS: 0x224000 - 0x236FFF, 77,824 bytes)
Contains platform_t which includes:
- VirtIO device structures
- VirtIO transport structures
- Virtqueue memory buffers
- IRQ routing table (256 entries)
- IDT entries (256 * 16 bytes = 4 KiB)
- ACPI state pointers

### g_user (BSS: 0x237000 - 0x23A9FF, 14,848 bytes)
User state structure

### Stack (BSS: 0x2130F0 - 0x2230F0, 64 KiB)
Kernel stack, grows downward from 0x2230F0

## Potential Issues

### 1. **CRITICAL: Page Table Overlap with BIOS**
```
Page tables: 0x9000 - 0xDFFF (20 KiB)
```
This region overlaps with Extended BIOS Data Area (EBDA), which typically starts around 0x9FC00 but can vary. On some systems, EBDA can extend down to 0x9000.

**Risk**: BIOS/firmware may write to EBDA during runtime, corrupting page tables.

### 2. **ACPI Table Location**
```
RSDP at 0x10E000 (discovered)
```
This is in the "free RAM" area between kernel end (0x23DC00) and top of memory. However, we don't know the full extent of ACPI tables.

### 3. **PCI BAR Allocation**
```
pci_next_bar_addr = 0xC0000000
```
Starting point is correct (mapped region), but allocate_pci_bars() doesn't check:
- Upper bound (should not exceed 0xD0000000)
- Alignment conflicts
- Overlaps with existing MMIO regions

### 4. **Missing Memory Protection**
- No checks prevent kernel from writing to:
  - BIOS data area
  - ACPI tables
  - MMIO regions (except via page table permissions)
- No heap allocator, so no runtime memory management

### 5. **Identity Mapping Limited to 4 MiB**
```
boot.S maps: 0x000000 - 0x3FFFFF (4 MiB)
```
Only covers:
- Low memory + BIOS
- Kernel (0x200000 - 0x23DC00)

Any access beyond 0x3FFFFF in low memory will page fault unless explicitly mapped.

## **ROOT CAUSE: PCI BAR Memory Corruption (USE_PCI=1 on q35) - FIXED**

### The Bug (Historical)

When running with `USE_PCI=1` on q35 machine, severe memory corruption occurred:
- Strings in .rodata got corrupted with random bytes
- Output showed garbled text: `[RNG] Allocating � 0x...` instead of `[RNG] Allocating BARs starting at 0x...`
- Missing newlines and brackets throughout output

### Root Cause

The code was **overwriting QEMU's pre-assigned BARs** with hardcoded addresses:

```c
// OLD CODE (BROKEN):
platform->pci_next_bar_addr = 0xC0000000;  // Hardcoded!
// ... then overwrites all BARs with 0xC0000000-based addresses
```

**What went wrong:**
1. QEMU q35 firmware **already assigned BARs** at valid addresses
2. Our code **overwrote them** with 0xC0000000-based addresses
3. MSI-X table writes went to **wrong addresses** (possibly physical RAM at 0xC000, corrupting page directory)
4. Memory corruption ensued in .rodata section

### The Fix

Modified `allocate_pci_bars()` in platform/x64/platform_virtio.c to:
1. **Read existing BAR values first** from PCI config space
2. **Use them if valid** (trust QEMU/firmware assignments)
3. **Only allocate new addresses** if BAR is unassigned (0)
4. **Validate addresses** are in valid MMIO range (0xC0000000-0xFEC00000)

```c
// NEW CODE (FIXED):
// Read existing BAR address
uint64_t existing_addr = /* read from PCI config */;

// If already assigned and valid, use it
if (existing_addr != 0 && is_valid_bar_address(existing_addr)) {
  continue; // Keep existing assignment
}

// Only allocate if unassigned or invalid
```

### Verification

After fix, PCI mode works correctly on q35:
```
[RNG] Using QEMU-assigned BARs
[BLK] Using QEMU-assigned BARs
[NET] Using QEMU-assigned BARs
[TEST PASS] RNG
```

All devices initialize, no corruption, all tests pass.

### Why MMIO Mode (USE_PCI=0) Always Worked

- microvm machine: No PCI bus, uses MMIO devices at 0xFEB00000+
- No BAR allocation code runs
- No MSI-X table writes to potentially wrong addresses
- VirtIO MMIO devices are at known, valid MMIO addresses

## How PCI BAR Allocation Works Now

### Proper PCI Discovery Sequence

The fixed implementation follows industry-standard PCI enumeration:

1. **Read existing BARs from PCI config space**
   - QEMU/firmware may have already assigned addresses
   - BAR value 0 or 0xFFFFFFFF = unassigned

2. **Check if existing address is valid**
   - Must be non-zero
   - Must be in valid MMIO range (0xC0000000-0xFEC00000 for x64)
   - If valid: **USE IT** (trust firmware)

3. **Allocate only if unassigned**
   - Probe BAR size by writing all 1s
   - Align to size requirement
   - Validate allocated address is in valid range
   - Write new address to PCI config space

### Code Flow

```c
// Read existing BAR value
uint64_t existing_addr = platform_pci_read_bar(...);

// Trust QEMU/firmware if already assigned
if (existing_addr != 0 && is_valid_bar_address(existing_addr)) {
    printk("Using QEMU-assigned BARs");
    return; // Keep existing assignment
}

// Only allocate if unassigned (bare metal scenario)
uint64_t new_addr = allocate_from_pool(...);
if (is_valid_bar_address(new_addr)) {
    platform_pci_config_write32(..., new_addr);
}
```

### Why This Approach Works

**For QEMU VMs (q35):**
- Firmware (SeaBIOS/OVMF) pre-assigns BARs during POST
- We read and use these assignments
- No manual allocation needed
- MSI-X table writes go to correct MMIO addresses

**For bare metal (if needed):**
- BARs are unassigned (0x00000000)
- We allocate from pool starting at 0xC0000000
- Addresses are validated before use
- Falls back gracefully

**Validation prevents corruption:**
- Rejects addresses below 0xC0000000 (likely RAM)
- Rejects addresses above 0xFEC00000 (IOAPIC/LAPIC region)
- Ensures MSI-X writes only go to valid MMIO space

## Current Status

**Both modes now work correctly:**
```bash
make run PLATFORM=x64 USE_PCI=0  # MMIO mode (microvm)
make run PLATFORM=x64 USE_PCI=1  # PCI mode (q35) - FIXED!
```

**PCI mode output (q35):**
```
[RNG] Using QEMU-assigned BARs
[BLK] Using QEMU-assigned BARs
[NET] Using QEMU-assigned BARs
[TEST PASS] RNG
```

**MMIO mode output (microvm):**
```
Found VirtIO-Net at MMIO 0xFEB02A00
Found VirtIO-Block at MMIO 0xFEB02C00
Found VirtIO-RNG at MMIO 0xFEB02E00
[TEST PASS] RNG
```

## Tools for Verification

```bash
# Inspect kernel sections
llvm-objdump -h build/x64/kernel.elf
llvm-nm -S -n build/x64/kernel.elf

# Disassemble specific addresses
llvm-objdump -d build/x64/kernel.elf | grep -A10 "0020c000"

# Check size of structures
echo 'p sizeof(platform_t)' | lldb build/x64/kernel.elf
```

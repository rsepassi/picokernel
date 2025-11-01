# x86 Platform Memory Map

## x64 (64-bit)

### Physical Memory Layout

**Low Memory (0x00000000 - 0x000FFFFF) - 1 MiB**
```
0x00000000 - 0x00007FFF:  BIOS Data Area (32 KiB)
0x00009000 - 0x0000DFFF:  Page Tables (20 KiB)
                          - PML4 at 0x9000
                          - PDPT at 0xA000
                          - PD0 at 0xB000 (maps 0-1GB)
                          - PD3 at 0xC000 (maps 3-4GB MMIO)
0x000E0000 - 0x000FFFFF:  Extended BIOS Area
```

**Kernel Memory (0x00200000+)**
```
0x00200000:               Kernel base (.text.boot)
                          - .text section (~49 KiB)
                          - .rodata (page-aligned, ~7 KiB)
                          - .bss (page-aligned, ~187 KiB)
                            - IDT (4 KiB)
                            - g_kernel (~76 KiB)
                            - g_user (~14.5 KiB)
                            - stack (64 KiB)
```

**Memory-Mapped I/O**
```
0xC0000000 - 0xCFFFFFFF:  PCI BAR MMIO (256 MiB, cache-disabled)
0xFE000000 - 0xFEFFFFFF:  High MMIO (16 MiB)
0xFEB00000 - 0xFEB0FFFF:  VirtIO MMIO devices (64 KiB)
0xFEC00000 - 0xFEC00FFF:  IOAPIC #0
0xFEC10000 - 0xFEC10FFF:  IOAPIC #1
0xFEE00000 - 0xFEE00FFF:  Local APIC
```

### Key Details

**Page Tables**: 4-level hierarchy (PML4 → PDPT → PD → PT), identity-mapped first 4 MiB

**QEMU Machines**:
- `microvm`: VirtIO MMIO devices at 0xFEB00000+
- `q35`: PCI VirtIO devices, BARs pre-assigned by firmware

**Boot Sequence**:
1. `boot.S` sets up GDT, IDT, page tables
2. Enters long mode (64-bit)
3. Maps kernel and MMIO regions
4. Jumps to `kmain`

### Debug Tools

**LLDB scripts**: `platform/x86/script/debug/lldb_x86.txt`

**Memory validation**:
```c
platform_mem_validate_critical();  // Check page tables, kernel sections
platform_mem_dump_pagetables();    // Walk PML4 → PDPT → PD → PT
```

## x32 (32-bit)

Similar layout to x64 but with 32-bit addressing:
- Kernel at 0x00200000
- 2-level page tables (PD → PT)
- No PML4/PDPT levels
- Same MMIO regions

### Known Issues

**Page Table Location**: At 0x9000, may overlap with EBDA on some systems
**PCI BAR Allocation**: Use QEMU-assigned BARs when available (see `script/debug/MEMORY_MAP_x64.md` for details)

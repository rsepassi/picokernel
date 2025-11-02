# Memory Management Plan

## Overview

This document outlines the plan for discovering and tracking available memory regions across all platforms (ARM64, RV64, x64) in VMOS. The goal is to build a list of free memory regions after accounting for reserved areas (kernel, DTB, stack, page tables). This provides the foundation for future memory allocation.

Since we run in a single address space, the focus is on discovering available physical memory and ensuring proper memory attributes through basic MMU configuration.

## Key Design Principles

### 1. NO HARDCODED ADDRESSES

**NEVER hardcode memory addresses, MMIO regions, or device locations.** Always discover them from:
- **ARM64/RV64**: Device tree (FDT) passed at boot + linker symbols
- **x64**: Multiboot info structure + linker symbols

The only acceptable uses of specific addresses:
- Linker symbols (`_start`, `_end`, `stack_top`, etc.)
- Boot-time register values (x0/a0 for FDT pointer, a1 for FDT on RV64, EAX/EBX for multiboot)
- Temporarily assuming DTB size limits until FDT header is parsed

All memory regions, RAM size, UART location, interrupt controller addresses, PCI/MMIO device bases must be discovered at runtime.

### 2. PARSE ONCE, USE MANY TIMES

**Parse device tree (FDT) or multiboot info EXACTLY ONCE** during platform initialization. Create a single traversal function that populates a comprehensive structure with ALL needed information:

```c
// Example for FDT-based platforms (ARM64, RV64)
typedef struct {
    mem_region_t mem_regions[KCONFIG_MAX_MEM_REGIONS];
    int num_mem_regions;
    uintptr_t uart_base;
    uintptr_t interrupt_controller_base;
    uintptr_t pci_ecam_base;
    // ... all other addresses discovered from FDT
} platform_fdt_info_t;

int platform_fdt_parse_once(void *fdt, platform_fdt_info_t *info);

// Example for multiboot-based platforms (x64)
typedef struct {
    mem_region_t mem_regions[KCONFIG_MAX_MEM_REGIONS];
    int num_mem_regions;
    uintptr_t mboot_info_base;
    size_t mboot_info_size;
    // ... all other info from multiboot
} platform_multiboot_info_t;

int platform_multiboot_parse_once(uint32_t magic, multiboot_info_t *mboot,
                                   platform_multiboot_info_t *info);
```

**DO NOT** repeatedly traverse the FDT or multiboot structure for different pieces of information. Extract everything in one pass and store it in a well-structured format.

**Why this matters**:
- Efficiency: Parsing is expensive, do it once
- Correctness: Single source of truth, no inconsistencies
- Maintainability: All discovery logic in one place
- Safety: FDT/multiboot data may be in memory we want to reclaim later

## Current Memory Layout (QEMU ARM64 virt)

**IMPORTANT**: The addresses below are typical QEMU defaults and should NOT be hardcoded. Always discover actual addresses from the device tree (FDT) where possible.

```
0x40000000 - 0x401FFFFF   DTB region (2 MiB reserved, location passed at boot)
0x40200000 - 0x4020XXXX   Kernel (.text, .rodata, .data, .bss - use linker symbols)
0x4020XXXX - 0x4030XXXX   Stack (64 KiB, grows down - use linker symbols)
0x4030XXXX - 0x48000000   Available for allocation (size varies with QEMU -m flag)

0x08000000 - 0x08020000   GIC (interrupt controller - discover from FDT)
0x09000000 - 0x09001FFF   UART (discover from FDT)
0x0A000000+               VirtIO MMIO devices (discover from FDT)
0x4010000000+             PCI ECAM (discover from FDT if USE_PCI=1)
```

## Implementation Steps

### 1. Memory Discovery

**CRITICAL DESIGN PRINCIPLE**: Parse device tree (FDT) or multiboot info EXACTLY ONCE during platform initialization. The single traversal should populate a comprehensive structure with ALL needed information (memory regions, MMIO addresses, device locations, etc.). DO NOT repeatedly traverse the same data structure for different information - this is inefficient and error-prone.

Extend existing FDT parsing in `platform/shared/devicetree.c`:

```c
typedef struct {
    uintptr_t base;
    size_t size;
} mem_region_t;

// Single-pass FDT parsing - populates comprehensive platform info structure
typedef struct {
    mem_region_t mem_regions[KCONFIG_MAX_MEM_REGIONS];
    int num_mem_regions;
    uintptr_t uart_base;      // UART MMIO base from FDT
    uintptr_t gic_base;       // GIC base (ARM64)
    uintptr_t pci_ecam_base;  // PCI ECAM base (if USE_PCI=1)
    // ... other device addresses discovered from FDT
} platform_fdt_info_t;

int platform_fdt_parse_once(void *fdt, platform_fdt_info_t *info);
```

Parse all `memory@` nodes from device tree to discover RAM regions. Support up to `KCONFIG_MAX_MEM_REGIONS` discontiguous regions. QEMU typically provides a single region at `0x40000000` (ARM64) or `0x80000000` (RV64) with size based on `-m` flag (default 128M), but we handle multiple regions for generality.

**Implementation**: Create a single FDT traversal function that walks the device tree once and extracts:
- All memory@ nodes → mem_regions array
- UART/serial device → uart_base
- Interrupt controller (GIC/PLIC) → controller base addresses
- PCI or VirtIO MMIO devices → device base addresses

This populated structure is then used throughout platform initialization without re-parsing the FDT.

### 2. MMU Configuration (Critical)

**Why needed**: ARM64 requires page tables to configure memory attributes. Without MMU setup:
- No cache coherency control
- Poor performance (no caching)
- Device memory accesses may behave incorrectly

**What to configure**:

#### a) Memory Attribute Indirection Register (MAIR_EL1)
Define memory types for Normal (cached RAM) and Device (uncached MMIO):

```assembly
// MAIR encoding: Device-nGnRnE at index 1, Normal WB at index 0
ldr x0, =0x000000000044ff04
msr mair_el1, x0
```

#### b) Translation Control Register (TCR_EL1)
Configure page table parameters (64KB granule, 48-bit address space):

```assembly
// TCR_EL1 for 64KB pages, 48-bit address space
// - TG0 = 01 (64KB granule)
// - T0SZ = 16 (48-bit address space: 64 - 16 = 48)
// - 3-level page tables (L1/L2/L3)
ldr x0, =0x0000000080803510  // TODO: Update for 64KB config
msr tcr_el1, x0
```

#### c) Identity-Mapped Page Tables
Create minimal page table hierarchy mapping:
- RAM (`0x40000000-0x48000000`): Normal memory with caching
- MMIO (`0x08000000-0x0A200000`): Device memory
- PCI ECAM (`0x4010000000+`): Device memory (if USE_PCI=1)

With 64KB pages and 48-bit address space:
- L1 table: 8192 entries, each covers 512 GB
- L2 table: 8192 entries, each covers 64 MB
- L3 table: 8192 entries, each covers 64 KB

Page table descriptors:
```c
#define PTE_VALID      (1UL << 0)
#define PTE_TABLE      (1UL << 1)  // Upper levels
#define PTE_PAGE       (1UL << 1)  // Level 3
#define PTE_AF         (1UL << 10) // Access flag
#define PTE_NORMAL     (0UL << 2)  // MAIR index 0
#define PTE_DEVICE     (1UL << 2)  // MAIR index 1
```

Allocate page tables statically in BSS:
```c
static uint64_t page_tables[5][8192] __attribute__((aligned(65536)));
```

#### d) Enable MMU (SCTLR_EL1)
```assembly
mrs x0, sctlr_el1
orr x0, x0, #(1 << 0)  // M: Enable MMU
orr x0, x0, #(1 << 2)  // C: Enable data cache
orr x0, x0, #(1 << 12) // I: Enable instruction cache
msr sctlr_el1, x0
isb
```

**Note**: Check current state first - QEMU may already enable MMU.

### 3. Reserved Region Tracking

Identify all reserved regions using dynamic discovery:
- DTB: Location from register x0 at boot, size from FDT header (do NOT hardcode address)
- Kernel: From linker symbols `_start` to `_end` (NEVER hardcode kernel addresses)
- Stack: `stack_bottom - stack_top` (from linker symbols, 64 KiB)
- Page tables: Region after `_end` (size TBD based on mapping)

**Critical**: Use linker symbols and boot-time registers for all addresses. The only exception is temporarily reserving extra DTB space if needed (e.g., 2 MiB) until FDT header is parsed.

### 4. Build Free Region List

Create an array of available memory regions by subtracting reserved regions from total RAM:

```c
typedef struct {
    uintptr_t base;
    size_t size;
} mem_region_t;
```

**Algorithm**:
1. Start with total RAM region(s) from FDT (e.g., `0x40000000 - 0x48000000`)
2. Subtract each reserved region in order
3. This may split a region into multiple parts
4. Result: array of free, non-overlapping regions stored in `platform_t`

**Example** (using discovered addresses, NOT hardcoded):
```
Initial:     [0x40000000 - 0x48000000]  (128 MiB, from FDT memory@ node)
- DTB:       [0x40000000 - 0x401FFFFF]  (from x0 register + FDT size)
- Kernel:    [0x40200000 - 0x4020B000]  (from linker symbols _start, _end)
- Stack:     [0x4020B000 - 0x4021B000]  (from linker symbols)
- Page tbl:  [0x4021B000 - 0x4021F000]  (allocated after _end)

Result:      [0x4021F000 - 0x48000000]  (~126 MiB free)
```

**Note**: All addresses in this example are discovered at runtime, not compiled in.

Store the array in `platform_t` for later use by kernel allocators.

### 5. Implementation Files

New files to create:
- `platform/arm64/platform_mem.c`: MMU setup and region list building (integrated into `platform_init()`)
- Extend `platform/shared/devicetree.c`: Add `platform_fdt_find_memory()`
- Update `kernel/kconfig.h`: Add `KCONFIG_MAX_MEM_REGIONS` (default 16)

API in `platform.h`:
```c
typedef struct {
    uintptr_t base;
    size_t size;
} mem_region_t;

typedef struct {
    mem_region_t *regions;  // Pointer to platform-managed array
    int count;              // Number of regions
} mem_region_list_t;

// Called after platform_init() completes
mem_region_list_t platform_mem_regions(void);
```

Add to `platform_t`:
```c
mem_region_t mem_regions[KCONFIG_MAX_MEM_REGIONS];
int num_mem_regions;
```

Kernel allocator interface in `kapi.h`:
```c
typedef struct kalloc {
    // Allocator-specific state (visible, not opaque)
    // ... members depend on allocator type ...
} kalloc_t;

void* kalloc_alloc(kalloc_t *alloc, size_t size, size_t align);
void  kalloc_free(kalloc_t *alloc, void *ptr);

// Init functions - initialize stack or static-allocated kalloc_t
void kalloc_init_bump(kalloc_t *alloc, mem_region_list_t regions);
void kalloc_init_buddy(kalloc_t *alloc, mem_region_list_t regions);
```

## Testing Strategy

1. Add debug code to check MMU status registers (SCTLR, MAIR, TCR, TTBR)
2. Verify memory regions discovered from FDT match QEMU config (`-m 128M`)
3. Print all reserved regions with their addresses and sizes
4. Print final free region list - verify:
   - No overlaps with reserved regions
   - No gaps (all RAM accounted for)
   - Proper alignment of base addresses
5. Check that cache is functioning (performance difference with/without MMU)
6. Validate no corruption of kernel sections (existing checksums)

## Deliverables

After completion:
1. **Platform layer** (`platform_t`) will contain:
   - `mem_region_t mem_regions[KCONFIG_MAX_MEM_REGIONS]`: Array of available memory regions
   - `int num_mem_regions`: Count of available regions
   - Each entry describes a contiguous free region (base address + size)
   - All reserved regions properly excluded
   - Memory attributes configured via MMU for proper caching

2. **Kernel layer** can query regions via `platform_mem_regions()` and initialize allocators:
   ```c
   void kmain_init(void) {
       platform_init();  // Discovers memory, sets up MMU, builds free list

       mem_region_list_t regions = platform_mem_regions();
       kalloc_init_bump(&g_kernel.allocator, regions);  // Or buddy, etc.

       // ... rest of init
   }
   ```

This provides the foundation for implementing actual allocators (bump, free list, buddy, etc.) in the kernel layer.

## Design Decisions (ARM64)

1. **Address Discovery**: ALWAYS discover addresses from FDT (device tree) and linker symbols. NEVER hardcode memory addresses, MMIO regions, or device locations.
2. **Initialization Flow**: Memory discovery and MMU setup integrated into `platform_init()`, kernel queries via `platform_mem_regions()`
3. **MMU Configuration**: Configure from scratch, assume QEMU starts with MMU disabled
4. **Page Table Configuration**: 64KB page granule, 48-bit address space (3 levels: L1/L2/L3)
5. **Page Table Allocation**: Static array in BSS: `static uint64_t page_tables[N][8192] __attribute__((aligned(65536)))`
6. **Region Storage**: Static array in `platform_t`: `mem_region_t mem_regions[KCONFIG_MAX_MEM_REGIONS]` (configurable in kconfig.h, default 16)
7. **RAM Region Support**: Parse all `memory@` nodes from FDT, support up to `KCONFIG_MAX_MEM_REGIONS` discontiguous regions
8. **Code Location**: `platform/arm64/platform_mem.c` (memory discovery and MMU setup), integrated into platform initialization

## RV64 Considerations

### Key Differences from ARM64

RISC-V 64-bit has a simpler but different MMU architecture compared to ARM64. Key differences:

#### 1. Page Table Modes

RV64 supports three MMU modes via the `satp` (Supervisor Address Translation and Protection) register:
- **Sv39**: 39-bit virtual addresses, 3-level page tables (most common)
- **Sv48**: 48-bit virtual addresses, 4-level page tables
- **Sv57**: 57-bit virtual addresses, 5-level page tables (rare)

**Decision**: Use **Sv39** for simplicity and broad compatibility. QEMU virt machine defaults to Sv39.

#### 2. Page Size and Structure

Unlike ARM64's configurable page granule (4KB/16KB/64KB), RV64 uses:
- **Fixed 4KB pages** (4096 bytes)
- 512 entries per page table (each PTE is 8 bytes)
- Each page table is exactly 4KB (512 × 8 bytes)

**Sv39 hierarchy**:
- L2 (root): 512 entries, each covers 1 GB (30 bits)
- L1: 512 entries, each covers 2 MB (21 bits)
- L0 (leaf): 512 entries, each covers 4 KB (12 bits)

#### 3. Physical Memory Layout (QEMU virt)

**IMPORTANT**: The addresses below are typical QEMU defaults and should NOT be hardcoded. Always discover actual addresses from the device tree (FDT) where possible.

```
0x00001000 - 0x00012000   DTB region (~68 KB, actual size varies - location from a1 register)
0x80000000 - 0x80200000   Kernel region (.text, .rodata, .data, .bss - use linker symbols)
0x80200000 - 0x80210000   Stack (64 KiB, grows down - use linker symbols)
0x80210000 - 0x88000000   Available for allocation (size varies with QEMU -m flag)

0x0c000000 - 0x0c200000   VirtIO MMIO devices (discover from FDT)
0x10000000 - 0x10000100   UART (16550 - discover from FDT)
0x30000000 - 0x40000000   PCI ECAM (discover from FDT if USE_PCI=1)
```

**Key differences**:
- RAM starts at `0x80000000` (vs `0x40000000` on ARM64) - verify from FDT, don't assume
- DTB is at low memory `0x00001000` (not in RAM region) - location passed in a1 register
- Kernel loaded at start of RAM (`0x80000000`) - use linker symbols, not hardcoded addresses

#### 4. MMU Configuration Registers

RV64 has a single control register instead of ARM64's multiple registers:

**satp (Supervisor Address Translation and Protection)**:
```
[63:60] MODE: 0=Bare (no translation), 8=Sv39, 9=Sv48, 10=Sv57
[59:44] ASID: Address Space ID (0 for single address space)
[43:0]  PPN:  Physical Page Number of root page table
```

**Enable MMU**:
```assembly
# Assume page table root at pt_root (physical address)
li t0, (8 << 60)              # MODE = Sv39
srli t1, <pt_root>, 12        # Convert address to PPN
or t0, t0, t1                 # Combine MODE and PPN
csrw satp, t0                 # Write to satp
sfence.vma                    # Flush TLB
```

**No separate registers for**:
- Memory attributes (handled by PTE flags)
- Cache control (caching controlled by page table)
- Page granule configuration (always 4KB)

#### 5. Page Table Entry Format

RV64 PTE (8 bytes, 64 bits):
```
[63:54] Reserved (set to 0)
[53:28] PPN[2] (26 bits for Sv39)
[27:19] PPN[1] (9 bits)
[18:10] PPN[0] (9 bits)
[9:8]   RSW (Reserved for Software)
[7]     D (Dirty)
[6]     A (Accessed)
[5]     G (Global)
[4]     U (User accessible)
[3]     X (Executable)
[2]     W (Writable)
[1]     R (Readable)
[0]     V (Valid)
```

**Key flags**:
```c
#define PTE_V     (1UL << 0)  // Valid
#define PTE_R     (1UL << 1)  // Readable
#define PTE_W     (1UL << 2)  // Writable
#define PTE_X     (1UL << 3)  // Executable
#define PTE_A     (1UL << 6)  // Accessed
#define PTE_D     (1UL << 7)  // Dirty

// Page table pointer (non-leaf)
#define PTE_TABLE (PTE_V)     // V=1, R=W=X=0

// Normal memory (RAM): readable, writable, accessed, dirty
#define PTE_RAM   (PTE_V | PTE_R | PTE_W | PTE_A | PTE_D)

// Device memory (MMIO): readable, writable, accessed, dirty
// Note: RV64 doesn't distinguish device vs normal via PTE
// Cache behavior controlled by Physical Memory Attributes (PMA)
#define PTE_MMIO  (PTE_V | PTE_R | PTE_W | PTE_A | PTE_D)
```

**Important**: Unlike ARM64's MAIR system, RV64 doesn't have explicit cache control in PTEs. Caching behavior is determined by:
- **Physical Memory Attributes (PMA)**: Platform-defined, typically hardwired in hardware
- QEMU automatically treats MMIO regions as uncached based on physical address ranges

#### 6. Memory Attributes and Caching

**ARM64**: Uses MAIR_EL1 to define memory types (Normal cached, Device uncached)

**RV64**: Uses Physical Memory Attributes (PMA), which are:
- Defined by platform (not software-configurable)
- Hardwired based on physical address ranges
- QEMU automatically handles:
  - RAM regions: Cached, coherent
  - MMIO regions: Uncached, strict ordering

**Implication**: No need for explicit memory type configuration. PTEs just specify permissions (R/W/X).

#### 7. Reserved Regions for RV64

**Note**: Discover all addresses from FDT and linker symbols, NOT hardcoded values.

```
Initial RAM: [0x80000000 - 0x88000000]  (from FDT memory@ node, size varies with -m)

Reserved (discovered at runtime):
- Kernel:     [_start - _end]            (from linker symbols, NEVER hardcode)
- Stack:      [stack_bottom - stack_top] (64 KiB, from linker symbols)
- Page tables: Region after _end         (size depends on mapping)

DTB: Location from a1 register (typically 0x00001000, outside RAM region)

Result: [<end of page tables> - <end of RAM from FDT>]  (~126-127 MiB free)
```

**Critical**: RAM size varies with QEMU -m flag. Parse FDT memory@ node for actual size.

### Design Decisions (RV64)

1. **Address Discovery**: ALWAYS discover addresses from FDT (device tree) and linker symbols. NEVER hardcode memory addresses, MMIO regions, or device locations.
2. **Initialization Flow**: Same as ARM64, integrated into `platform_init()`, kernel queries via `platform_mem_regions()`
3. **MMU Mode**: Sv39 (39-bit virtual addresses, 3-level page tables)
4. **Page Size**: 4KB (fixed in RV64 specification)
5. **Page Table Configuration**:
   - L2 table: 512 entries (root)
   - L1 table: 512 entries per L2 entry (allocate on demand or statically)
   - L0 table: 512 entries per L1 entry (allocate on demand or statically)
6. **Page Table Allocation**: Static array in BSS:
   ```c
   static uint64_t page_tables[N][512] __attribute__((aligned(4096)));
   ```
7. **Identity Mapping**: Map all usable physical memory 1:1 (virtual = physical)
8. **Memory Regions to Map** (discover from FDT, these are typical examples only):
   - RAM: From FDT memory@ node (size varies with QEMU -m flag)
   - UART: From FDT serial@ node (don't assume 0x10000000)
   - VirtIO MMIO: From FDT virtio@ nodes (don't assume 0x0c000000)
   - PCI ECAM: From FDT pcie@ node (don't assume 0x30000000, only if USE_PCI=1)
9. **Region Storage**: Same as ARM64, static array in `platform_t`: `mem_region_t mem_regions[KCONFIG_MAX_MEM_REGIONS]`
10. **Code Location**: `platform/rv64/platform_mem.c`

### Implementation Notes for RV64

1. **Check MMU Status**: Read `satp` register. If MODE field is non-zero, MMU is already enabled.
   ```assembly
   csrr t0, satp
   srli t0, t0, 60        # Extract MODE field
   bnez t0, mmu_enabled   # If non-zero, already enabled
   ```

2. **Minimal Page Tables**: For identity mapping with Sv39:
   - Need 1 L2 (root) table
   - Need L1 tables to cover RAM (128 MB needs 1 L1 table, covers up to 1 GB)
   - Need L0 tables to cover each 2 MB region (128 MB needs 64 L0 tables)
   - **Total**: 1 + 1 + 64 = 66 page tables × 4KB = 264 KB

3. **Sparse Mapping Option**: Instead of mapping all 128 MB at 4KB granularity:
   - Use 2 MB megapages (L1 leaf entries) where possible
   - Set R/W/X bits in L1 PTE instead of pointing to L0 table
   - **Savings**: 128 MB / 2 MB = 64 megapages → only 2 page tables (L2 + L1) = 8 KB
   - **Trade-off**: Less flexibility for future fine-grained permissions

4. **Recommended Approach**: Use 2 MB megapages for initial implementation:
   ```c
   static uint64_t pt_l2[512] __attribute__((aligned(4096)));  // Root
   static uint64_t pt_l1_ram[512] __attribute__((aligned(4096)));  // RAM region
   static uint64_t pt_l1_mmio[512] __attribute__((aligned(4096))); // MMIO region
   ```

5. **FDT Parsing**: Same `platform_fdt_find_memory()` API as ARM64, but expect:
   - RAM region at `0x80000000` with size from `-m` flag
   - DTB itself available via register passed to `_start` (a1 register)

### Testing Differences

- **MMU Check**: Verify `satp` register shows Sv39 mode (8) and correct PPN
- **Memory Layout**: Confirm RAM starts at `0x80000000` not `0x40000000`
- **DTB Location**: DTB parsing works from low memory (`0x00001000`)
- **Page Alignment**: All page table addresses aligned to 4KB (not 64KB)
- **Reserved Regions**: Verify DTB region is NOT subtracted from RAM (different physical region)

## x64 Considerations

### Key Differences from ARM64 and RV64

x64 has the most complex MMU architecture of the three platforms, with legacy compatibility and multiple paging modes. Key differences:

#### 1. Paging Modes

x64 supports multiple paging modes via control registers:
- **32-bit paging**: Legacy 2-level tables (not used in 64-bit mode)
- **PAE paging**: 3-level tables with 36-bit physical addresses (not used in long mode)
- **4-level paging (IA-32e)**: 48-bit virtual addresses (most common for x64)
- **5-level paging**: 57-bit virtual addresses (newer CPUs, rare)

**Decision**: Use **4-level paging** for broad compatibility. This is the standard mode for x64 long mode.

#### 2. Page Size and Structure

x64 uses fixed 4 KB base pages with support for huge pages:
- **4 KB pages**: Standard leaf pages (PTE level)
- **2 MB pages**: Huge pages (PDE level, PS bit set)
- **1 GB pages**: Huge pages (PDPTE level, PS bit set, requires PDPE1GB CPU feature)

**4-level hierarchy**:
- PML4 (Page Map Level 4): 512 entries, each covers 512 GB (39 bits)
- PDPT (Page Directory Pointer Table): 512 entries, each covers 1 GB (30 bits)
- PD (Page Directory): 512 entries, each covers 2 MB (21 bits)
- PT (Page Table): 512 entries, each covers 4 KB (12 bits)

Each table is 4096 bytes (512 × 8-byte entries).

#### 3. Physical Memory Layout (QEMU x86_64)

**Important**: Memory layout is discovered via multiboot memory map at runtime. The values below are typical for QEMU with -m 128M, but **do not hardcode these addresses**:

```
Typical multiboot memory map entries for QEMU x86_64:

Low RAM (Type 1 - Available):
0x00000000 - 0x0009FC00   Low memory (~639 KB, available but often skipped)

Reserved regions (Type 2 - Reserved):
0x0009FC00 - 0x00100000   VGA/BIOS ROM hole (varies, multiboot marks as reserved)

Extended RAM (Type 1 - Available):
0x00100000 - 0x????????   Extended RAM (size from QEMU -m flag, e.g., 128 MB = 0x08000000)

MMIO regions (Type 2 - Reserved):
0xB0000000 - 0xB0001000   VirtIO MMIO devices (if USE_PCI=0, check multiboot map)
0xFEC00000 - 0xFEC01000   I/O APIC
0xFEE00000 - 0xFEE01000   Local APIC
0xE0000000 - 0xF0000000   PCI ECAM (if USE_PCI=1)

High memory (Type 1 - Available, if RAM > 3.5 GB):
Above 4 GB if -m specifies > 3.5 GB (for this project, typically < 4 GB)
```

**Key characteristics**:
- RAM typically starts at 1 MB (`0x100000`) after VGA/BIOS hole
- Exact RAM size and hole locations discovered from multiboot memory map
- APIC devices at high addresses (above 0xFE000000)
- Multiboot bootloader provides accurate memory map including all holes and reserved regions

**Kernel load address**: Determined by linker script and multiboot header. Typically `0x100000` (1 MB) but can vary. Use linker symbols (`_start`, `_end`) not hardcoded addresses.

#### 4. MMU Configuration Registers

x64 requires multiple control registers to enable paging:

**CR3 (Control Register 3)**: Page table base register
```
[63:52] Reserved (must be 0)
[51:12] Physical base address of PML4 table (must be 4K-aligned)
[11:5]  Reserved (ignored)
[4]     PCD: Page-level Cache Disable
[3]     PWT: Page-level Write-Through
[2:0]   Reserved (ignored)
```

**CR0 (Control Register 0)**: Fundamental control bits
```
[31] PG: Paging Enable (must be 1 for paging)
[16] WP: Write Protect (supervisor can't write read-only pages)
[5]  NE: Numeric Error (x87 FPU error handling)
[4]  ET: Extension Type (always 1 on modern CPUs)
[0]  PE: Protection Enable (must be 1 for protected mode)
```

**CR4 (Control Register 4)**: Extended features
```
[12] LA57: 57-bit Linear Addresses (5-level paging, 0 for 4-level)
[7]  PGE: Page Global Enable (allows global pages)
[5]  PAE: Physical Address Extension (must be 1 for long mode)
[4]  PSE: Page Size Extensions (allows 4 MB pages in 32-bit mode)
```

**EFER MSR (Extended Feature Enable Register, MSR 0xC0000080)**:
```
[11] NXE: No-Execute Enable (allows PTE NX bit)
[10] LMA: Long Mode Active (read-only, set by CPU)
[8]  LME: Long Mode Enable (must be 1 before enabling paging)
```

**Enable paging sequence** (assuming already in long mode):
```assembly
# 1. Load page table base into CR3
mov rax, [pml4_base]
mov cr3, rax

# 2. Enable paging (CR0.PG = 1)
mov rax, cr0
or rax, (1 << 31)    # Set PG bit
or rax, (1 << 16)    # Set WP bit (write protect)
mov cr0, rax

# No need to set CR4.PAE or EFER.LME if already in long mode
# (bootloader already did this)
```

**Note**: QEMU typically starts x64 in long mode with paging already enabled. Check CR0.PG before assuming MMU is off.

#### 5. Page Table Entry Format

x64 PTE (8 bytes, 64 bits):
```
[63]    XD/NX: Execute Disable (if EFER.NXE=1)
[62:52] Available for software use
[51:12] Physical address (40 bits, up to 1 TB physical memory)
[11:9]  Available for software use
[8]     G: Global (not flushed on CR3 write, requires CR4.PGE)
[7]     PS: Page Size (1=huge page at PDE/PDPTE level, 0=points to next level)
[6]     D: Dirty (written by CPU on write)
[5]     A: Accessed (set by CPU on access)
[4]     PCD: Page Cache Disable
[3]     PWT: Page Write-Through
[2]     U/S: User/Supervisor (0=kernel only, 1=user accessible)
[1]     R/W: Read/Write (0=read-only, 1=writable)
[0]     P: Present (1=valid mapping)
```

**Key flags**:
```c
#define PTE_P      (1UL << 0)   // Present
#define PTE_RW     (1UL << 1)   // Read/Write
#define PTE_US     (1UL << 2)   // User/Supervisor
#define PTE_PWT    (1UL << 3)   // Page Write-Through
#define PTE_PCD    (1UL << 4)   // Page Cache Disable
#define PTE_A      (1UL << 5)   // Accessed
#define PTE_D      (1UL << 6)   // Dirty
#define PTE_PS     (1UL << 7)   // Page Size (huge page)
#define PTE_G      (1UL << 8)   // Global
#define PTE_NX     (1UL << 63)  // No Execute

// Page table pointer (non-leaf): P=1, RW=1, all other flags clear
#define PTE_TABLE  (PTE_P | PTE_RW)

// Normal RAM: Present, R/W, Accessed, Dirty, cacheable (PCD=PWT=0)
#define PTE_RAM    (PTE_P | PTE_RW | PTE_A | PTE_D)

// Device MMIO: Present, R/W, Accessed, Dirty, Cache Disable
#define PTE_MMIO   (PTE_P | PTE_RW | PTE_A | PTE_D | PTE_PCD)

// 2 MB huge page: Same as above but with PS bit
#define PTE_RAM_2M (PTE_RAM | PTE_PS)
#define PTE_MMIO_2M (PTE_MMIO | PTE_PS)
```

#### 6. Memory Attributes and Caching

**x64 cache control** uses a combination of:

1. **PTE flags** (PCD, PWT): Per-page cache control
   - PCD=0, PWT=0: Write-back (fastest, default for RAM)
   - PCD=0, PWT=1: Write-through
   - PCD=1, PWT=0: Uncached (typically for MMIO)
   - PCD=1, PWT=1: Uncached

2. **PAT (Page Attribute Table)**: 8 memory types indexed by PTE bits
   - Similar to ARM64's MAIR system
   - Index = PAT_bit(7) | PCD_bit(4) | PWT_bit(3) → 3-bit index into PAT MSR
   - PAT MSR (0x277): 8 entries × 8 bits each
   - Memory types: UC (Uncacheable), WC (Write-Combining), WT (Write-Through), WP (Write-Protected), WB (Write-Back)

3. **MTRRs (Memory Type Range Registers)**: Legacy system-wide memory type configuration
   - Not needed for simple identity mapping
   - Can be left at firmware defaults

**Recommended approach for VMOS**:
- Use default PAT (don't modify PAT MSR)
- RAM: PCD=0, PWT=0 (write-back, cached)
- MMIO: PCD=1 (uncached)
- This gives correct behavior without PAT programming

#### 7. Memory Discovery on x64

Unlike ARM64/RV64 which use device trees, x64 uses the PVH boot protocol to discover system information:

**Boot protocol**: VMOS x64 uses the **PVH (Para-Virtualized Hardware) boot protocol**, not classic multiboot. PVH is a modern Xen-derived boot protocol supported by QEMU that starts the kernel directly in 32-bit protected mode with minimal setup required.

**Memory discovery options for PVH**:

**Option 1: PVH start info structure** (primary method)
- PVH provides `struct hvm_start_info` pointer in EBX/RBX at boot
- Structure defined in Xen interface headers (see https://android.googlesource.com/kernel/common/+/9d1efccf5ec3/include/xen/interface/hvm/start_info.h)
- Contains:
  - `memmap_paddr`: Physical address of E820-style memory map
  - `memmap_entries`: Number of entries in memory map
  - `rsdp_paddr`: ACPI RSDP pointer (optional)
  - `cmdline_paddr`: Kernel command line
  - Module information

**Option 2: fw_cfg interface** (alternative/fallback)
- QEMU firmware configuration device for discovering system info
- Access via I/O ports 0x510 (selector) and 0x511 (data) on x86
- Can query memory layout, boot order, kernel cmdline, etc.
- More flexible but requires additional I/O port access

**Option 3: Fixed layout assumption** (debugging/fallback only)
- QEMU has predictable layout but this is fragile
- Different QEMU versions and -m flags can change layout
- Should only be used as absolute fallback

**Decision for VMOS**: Use **PVH start info structure** as primary method, with fw_cfg as optional fallback.

**CRITICAL DESIGN PRINCIPLE**: Parse PVH start info EXACTLY ONCE during platform initialization. The single traversal should populate a comprehensive structure with ALL needed information (memory map, ACPI tables, modules, etc.). DO NOT repeatedly traverse the start info or memory map - this is inefficient and error-prone.

```c
// PVH start info structure (from Xen interface)
struct hvm_start_info {
    uint32_t magic;             // Magic value: 0x336ec578 "xEn3"
    uint32_t version;           // Version of this structure
    uint32_t flags;             // SIF_xxx flags
    uint32_t nr_modules;        // Number of modules passed to domain
    uint64_t modlist_paddr;     // Physical address of module list
    uint64_t cmdline_paddr;     // Physical address of command line
    uint64_t rsdp_paddr;        // Physical address of RSDP
    uint64_t memmap_paddr;      // Physical address of memory map
    uint32_t memmap_entries;    // Number of memory map entries
    uint32_t reserved;
} __attribute__((packed));

#define HVM_START_MAGIC_VALUE 0x336ec578

// E820-style memory map entry (pointed to by memmap_paddr)
struct hvm_memmap_table_entry {
    uint64_t addr;              // Base address
    uint64_t size;              // Length of region
    uint32_t type;              // E820 type
    uint32_t reserved;
} __attribute__((packed));

// E820 memory types
#define E820_RAM        1       // Available RAM
#define E820_RESERVED   2       // Reserved
#define E820_ACPI       3       // ACPI reclaimable
#define E820_NVS        4       // ACPI NVS
#define E820_UNUSABLE   5       // Unusable

// Single-pass PVH info parsing - populates comprehensive platform info structure
typedef struct {
    mem_region_t mem_regions[KCONFIG_MAX_MEM_REGIONS];
    int num_mem_regions;
    uintptr_t pvh_info_start;    // PVH start info location (to reserve)
    size_t pvh_info_size;        // Size of start info structure
    uintptr_t memmap_start;      // Memory map location (to reserve)
    size_t memmap_size;          // Size of memory map array
    // ... other info extracted from PVH (modules, ACPI, etc.)
} platform_pvh_info_t;

// Single-pass parsing function
int platform_pvh_parse_once(struct hvm_start_info *pvh_info,
                             platform_pvh_info_t *info) {
    if (pvh_info->magic != HVM_START_MAGIC_VALUE) {
        panic("Invalid PVH start info magic");
    }

    // Save PVH structure location to reserve it
    info->pvh_info_start = (uintptr_t)pvh_info;
    info->pvh_info_size = sizeof(struct hvm_start_info);

    // Get memory map
    if (pvh_info->memmap_paddr == 0 || pvh_info->memmap_entries == 0) {
        panic("PVH memory map not provided");
    }

    struct hvm_memmap_table_entry *memmap =
        (void *)(uintptr_t)pvh_info->memmap_paddr;

    // Save memory map location to reserve it
    info->memmap_start = (uintptr_t)memmap;
    info->memmap_size = pvh_info->memmap_entries * sizeof(struct hvm_memmap_table_entry);

    // Single traversal - extract ALL available RAM regions
    info->num_mem_regions = 0;
    for (uint32_t i = 0; i < pvh_info->memmap_entries; i++) {
        if (memmap[i].type == E820_RAM) {  // Available RAM
            if (info->num_mem_regions < KCONFIG_MAX_MEM_REGIONS) {
                info->mem_regions[info->num_mem_regions].base = memmap[i].addr;
                info->mem_regions[info->num_mem_regions].size = memmap[i].size;
                info->num_mem_regions++;
            }
        }
    }

    // Extract other PVH info (ACPI, modules, etc.) in same pass
    if (pvh_info->rsdp_paddr != 0) {
        // Save ACPI RSDP pointer if needed
    }
    if (pvh_info->cmdline_paddr != 0) {
        // Save kernel command line if needed
    }

    return 0;
}
```

**Implementation**: Create a single PVH parsing function that extracts:
- Memory map (E820_RAM entries) → mem_regions array
- PVH start info structure location/size → to reserve this memory
- Memory map array location/size → to reserve this memory
- ACPI RSDP, modules, command line if needed

This populated structure is then used throughout platform initialization without re-parsing.

**PVH boot sequence**:
1. QEMU with `-kernel` flag loads kernel ELF and finds PVH note section
2. QEMU sets up minimal environment (protected mode, basic page tables)
3. Kernel entry point receives PVH start info pointer in EBX (32-bit) or RBX (64-bit)
4. Kernel's `_start` routine (platform/x64/boot.S) saves pointer before long mode transition
5. After entering long mode, pointer passed to kmain for memory discovery

**Why PVH over legacy boot protocols**:
- Modern, clean boot protocol designed for virtual machines
- No legacy BIOS baggage
- Direct entry to protected/long mode
- Standard memory map format (E820)
- Works with different QEMU memory sizes (-m flag)
- Discovers actual available regions, not assumptions
- Handles memory holes correctly (VGA, ACPI, reserved)

#### 8. Reserved Regions for x64

Memory layout is discovered via PVH E820 memory map, not hardcoded. Typical QEMU x86_64 layout with -m 128M:

```
PVH E820 memory map typically shows:
- Type 1 (E820_RAM):      [0x00000000 - 0x0009FC00]  (Low memory, ~639 KB)
- Type 2 (E820_RESERVED): [0x0009FC00 - 0x00100000]  (VGA/BIOS hole)
- Type 1 (E820_RAM):      [0x00100000 - 0x08000000]  (Extended RAM, 127 MB)
- Type 2 (E820_RESERVED): [0xE0000000 - 0xF0000000]  (PCI ECAM if USE_PCI=1)
- Type 2 (E820_RESERVED): [0xFEC00000 - 0xFEE01000]  (APIC regions)

Additional regions to reserve from available RAM:
- Kernel:         [<kernel_load> - _end]      (from linker symbols, typically 1 MB)
- Stack:          [stack_bottom - stack_top]  (64 KiB, from linker)
- Page tables:    [<after _end> - <pt_end>]   (size depends on mapping, ~12 KB for 2MB pages)
- PVH start info: Region containing hvm_start_info structure (very small, ~48 bytes)
- PVH memory map: Array of hvm_memmap_table_entry structures (typically <1 KB)

Result: Multiple free regions after subtracting reserved areas
- Low memory: Skip (too small and fragmented, <640 KB)
- Extended RAM: [<end of page tables> - <end of RAM per E820>]  (~126-127 MiB free)

Note: RAM size and layout vary based on QEMU -m flag. Parse PVH memory map, don't assume addresses.
```

**Important**: VGA hole (0xA0000-0x100000) is automatically marked as reserved (type 2) in the E820 memory map. Do not assume its location - rely on PVH info.

### Design Decisions (x64)

1. **Address Discovery**: ALWAYS discover addresses from PVH start info structure. NEVER hardcode memory addresses or MMIO regions. Parse PVH info EXACTLY ONCE and populate a comprehensive info structure.
2. **Initialization Flow**: Same as ARM64/RV64, integrated into `platform_init()`, kernel queries via `platform_mem_regions()`
3. **Paging Mode**: 4-level paging (PML4 → PDPT → PD → PT), 48-bit virtual addresses
4. **Page Size**: 4 KB standard, with 2 MB huge pages for large regions
5. **Page Table Configuration**:
   - PML4: 512 entries (root)
   - PDPT: 512 entries per PML4 entry
   - PD: 512 entries per PDPT entry
   - PT: 512 entries per PD entry (only if not using 2 MB huge pages)
6. **Page Table Allocation**: Static array in BSS:
   ```c
   static uint64_t pml4[512] __attribute__((aligned(4096)));
   static uint64_t pdpt[512] __attribute__((aligned(4096)));
   static uint64_t pd_ram[512] __attribute__((aligned(4096)));
   // Use 2 MB huge pages in PD to avoid needing PT arrays
   ```
7. **Identity Mapping**: Map all usable physical memory 1:1 (virtual = physical)
8. **Memory Regions to Map** (discover from PVH E820 map, these are typical examples only):
   - RAM: All E820_RAM regions from PVH memory map (size varies with `-m` flag)
   - Local APIC: Typically `0xFEE00000` but verify from E820/ACPI (if needed)
   - I/O APIC: Typically `0xFEC00000` but verify from E820/ACPI (if needed)
   - VirtIO MMIO: Check E820 map for actual region (if USE_PCI=0)
   - PCI ECAM: Check E820 map for actual region (if USE_PCI=1)
9. **Caching**:
   - RAM: PCD=0, PWT=0 (write-back, cached)
   - MMIO: PCD=1 (uncached)
   - Don't modify PAT MSR, use defaults
10. **Memory Discovery**: Parse PVH start info structure ONCE (required, no hardcoded addresses)
11. **Region Storage**: Same as ARM64/RV64, static array in `platform_t`: `mem_region_t mem_regions[KCONFIG_MAX_MEM_REGIONS]`
12. **Code Location**: `platform/x64/platform_mem.c`
13. **Boot Protocol**: PVH boot (QEMU -kernel), with PVH start info pointer provided in EBX/RBX

### Implementation Notes for x64

1. **Check MMU Status**: Read CR0.PG bit. If set, paging already enabled.
   ```assembly
   mov rax, cr0
   test rax, (1 << 31)  # Test PG bit
   jnz paging_enabled   # If set, already enabled
   ```

2. **Check Long Mode**: Read EFER.LMA to verify we're in 64-bit mode.
   ```assembly
   mov ecx, 0xC0000080  # EFER MSR
   rdmsr                # Result in EDX:EAX
   test eax, (1 << 10)  # Test LMA bit
   jz not_long_mode     # Should never happen in x64 build
   ```

3. **Minimal Page Tables with 2 MB Pages**:
   - 1 PML4 table (covers entire 512 GB space)
   - 1 PDPT table per 512 GB (need 1 for low memory)
   - 1 PD table per 1 GB (need 1 for 0-1 GB region covering RAM + MMIO)
   - **Total**: 1 + 1 + 1 = 3 page tables × 4 KB = 12 KB
   - Each PD entry with PS=1 maps 2 MB directly (no PT needed)

4. **Example Page Table Setup**:
   ```c
   // Example: Map RAM discovered from PVH E820 map using 2 MB pages
   // Assume PVH found extended RAM from ram_base to ram_base+ram_size
   // (e.g., 0x00100000 to 0x08000000 for 127 MB if QEMU -m 128M)

   uint64_t ram_base = /* from PVH E820_RAM entry */;
   uint64_t ram_size = /* from PVH E820_RAM entry */;

   // Align to 2 MB boundaries for huge pages
   uint64_t map_base = ram_base & ~0x1FFFFF;  // Round down to 2 MB
   uint64_t map_end = (ram_base + ram_size + 0x1FFFFF) & ~0x1FFFFF;  // Round up

   uint64_t num_pages = (map_end - map_base) / 0x200000;  // Number of 2 MB pages

   for (uint64_t i = 0; i < num_pages; i++) {
       uint64_t phys = map_base + (i * 0x200000);
       pd_ram[i] = phys | PTE_RAM_2M;  // 2 MB huge page
   }

   // PDPT[0] points to PD (assumes RAM in first 1 GB)
   pdpt[0] = ((uint64_t)pd_ram) | PTE_TABLE;

   // PML4[0] points to PDPT (assumes RAM in first 512 GB)
   pml4[0] = ((uint64_t)pdpt) | PTE_TABLE;

   // Note: If PVH E820 map reports multiple discontiguous RAM regions,
   // create separate PD tables for each or map them in different PD entries
   ```

5. **Handle VGA Hole**: First 2 MB page (0x00000000-0x00200000) contains VGA hole.
   - **Option 1**: Skip mapping it entirely (safest)
   - **Option 2**: Use 4 KB pages for fine-grained control (adds 1 PT table)
   - **Option 3**: Map entire 2 MB but don't use addresses in 0xA0000-0x100000

   **Recommended**: Option 1 (don't map first 2 MB, kernel starts at 1 MB anyway)

6. **MMIO Mapping**: Map device regions with PCD=1
   ```c
   // Map Local APIC at 0xFEE00000 (1 × 2 MB page)
   pd_mmio[511] = 0xFEE00000 | PTE_MMIO_2M;  // Last entry in a PD
   ```

7. **CR3 Loading**: Ensure PML4 base is physical address
   ```assembly
   mov rax, pml4        # Load PML4 address
   mov cr3, rax         # Load into CR3 (also flushes TLB)
   ```

### Implementation Sequence

1. **Early boot check** (platform/x64/boot.S handles most of this):
   - PVH boot starts in 32-bit protected mode
   - Save PVH start info pointer from EBX (done at boot.S, saved to ESI)
   - boot.S sets up page tables and transitions to long mode
   - boot.S passes saved info pointer to kmain in RDI
   - kmain should verify the pointer is valid (magic = 0x336ec578) and contains memory map

2. **Memory discovery** (must happen early):
   - Verify PVH start info structure has valid magic and memory map pointers
   - Parse PVH E820 memory map to discover all available RAM regions
   - Typical x64 layout: low memory (0-640K), extended (1MB-3.5GB), high (>4GB if present)
   - Store available E820_RAM regions for later processing
   - **Critical**: Do not use hardcoded addresses - memory size varies with QEMU -m flag

3. **Page table setup** (if building new tables):
   - Allocate PML4, PDPT, PD arrays in BSS
   - Build identity mappings using 2 MB pages for all discovered RAM regions
   - Set RAM entries with PTE_RAM_2M (cached)
   - Map known MMIO regions with PTE_MMIO_2M (uncached):
     - Local APIC, I/O APIC if needed
     - VirtIO MMIO or PCI ECAM based on USE_PCI

4. **Enable paging** (if not already enabled):
   - Load PML4 address into CR3
   - Set CR0.PG = 1
   - Verify by reading back CR0

5. **Build free region list**:
   - Start with available RAM regions from PVH E820 memory map
   - For each region with type=E820_RAM (type 1):
     - Subtract kernel region [kernel_start, _end]
     - Subtract stack region [stack_bottom, stack_top]
     - Subtract page table region (static arrays in BSS)
     - Subtract PVH start info region [pvh_info, pvh_info + sizeof(struct hvm_start_info)]
     - Subtract PVH memory map array [memmap_paddr, memmap_paddr + (memmap_entries * sizeof(entry))]
     - Avoid VGA hole (0xA0000-0x100000) - typically already marked E820_RESERVED in PVH map
   - Store resulting free region(s) in platform_t
   - May have multiple discontiguous regions (e.g., low memory + extended memory)

   **Important**: Save or copy PVH start info structure early in boot, as bootloader may have placed it in reclaimable memory. Some implementations place it in type=1 (E820_RAM) regions that we want to use.

### Testing for x64

- **PVH Check**: Verify PVH start info pointer is valid, magic = 0x336ec578
- **Memory Map Parsing**: Print all PVH E820 memory map entries with base, length, and type
- **Paging Check**: Verify CR0.PG=1, CR3 points to PML4, CR4.PAE=1
- **Long Mode**: Verify EFER.LMA=1
- **Memory Layout**: Verify RAM regions match PVH E820 memory map (do NOT assume 1 MB start)
- **Variable Memory Size**: Test with different QEMU -m values (64M, 128M, 256M, 512M)
- **Page Table Entries**: Verify RAM has PCD=0 (cached), MMIO has PCD=1 (uncached)
- **Page Alignment**: All page table addresses aligned to 4 KB
- **Huge Pages**: Verify PD entries have PS=1 for 2 MB mappings
- **Reserved Regions**: Verify VGA hole and MMIO regions marked E820_RESERVED in PVH map
- **Free Region List**: Verify no overlaps with kernel, stack, page tables, or PVH-reserved regions
- **Cache Performance**: Compare execution speed with/without caching enabled

### x64-Specific Challenges

1. **Legacy Compatibility**: x64 boot process with PVH starts in 32-bit protected mode, then transitions to long mode. Ensure we're in long mode before assuming 64-bit features.

2. **VGA Hole**: 640 KB - 1 MB region must be avoided. PVH E820 memory map marks this as E820_RESERVED (type 2). Do not assume exact addresses - parse E820 map.

3. **APIC Addresses**: Local APIC and I/O APIC at high addresses (0xFEE00000, 0xFEC00000). Must map as device memory if accessed. Typically marked as E820_RESERVED in PVH map.

4. **Multiple Memory Regions**: Unlike ARM64/RV64, x64 typically has memory holes and multiple discontiguous regions (low memory, extended RAM, possibly high RAM above 4 GB). Must handle all regions from PVH E820 map, not assume single contiguous region.

5. **Bootloader Variance**: Different QEMU versions and configurations may provide different memory layouts and MMIO mappings. CRITICAL: Do not hardcode memory addresses - always parse PVH E820 memory map. Test with various QEMU -m values.

6. **Variable RAM Start**: While typically at 1 MB (0x100000), the exact start and size of available RAM depends on firmware. Use PVH E820 map, not assumptions.

7. **PVH Info Lifetime**: Bootloader may place PVH start info structure and E820 memory map in reclaimable RAM. Save/copy them early before memory allocation begins.

### Comparison Summary

| Feature | ARM64 | RV64 | x64 |
|---------|-------|------|-----|
| Page table levels | 3 (64KB pages) | 3 (Sv39) | 4 (standard) |
| Page size | 64 KB | 4 KB | 4 KB (2 MB huge) |
| Control registers | SCTLR, TCR, MAIR, TTBR | satp | CR0, CR3, CR4, EFER |
| Memory attributes | MAIR index in PTE | PMA (hardwired) | PTE flags + PAT |
| RAM base | 0x40000000 (fixed) | 0x80000000 (fixed) | Variable (1MB typical) |
| DTB location | In RAM (0x40000000) | Low mem (0x1000) | N/A (PVH) |
| Memory discovery | FDT parsing | FDT parsing | PVH E820 map required |
| Huge page support | Optional | Optional (2 MB) | Common (2 MB, 1 GB) |
| Boot protocol | Device tree in register | Device tree in register | PVH start info ptr |

### Additional Resources

- Intel® 64 and IA-32 Architectures Software Developer's Manual, Volume 3A: System Programming Guide (Chapter 4: Paging)
- AMD64 Architecture Programmer's Manual, Volume 2: System Programming
- OSDev Wiki: [Paging](https://wiki.osdev.org/Paging), [Higher Half x86 Bare Bones](https://wiki.osdev.org/Higher_Half_x86_Bare_Bones)

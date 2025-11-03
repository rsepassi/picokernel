# x64 Platform Memory Map

## Overview

The x64 platform is a 64-bit x86 architecture implementation supporting two QEMU machine types:
- **microvm**: Minimal machine with VirtIO MMIO devices (USE_PCI=0)
- **q35**: Full machine with PCI VirtIO devices (USE_PCI=1)

The kernel uses identity mapping with 2MB huge pages for the first 4GB of physical address space. No MMU is configured beyond basic paging setup - the kernel runs entirely in kernel space with direct physical memory access.

## Boot Information

### PVH Boot Protocol

The kernel boots via the PVH (Para-Virtualized Hardware) protocol, which:
- Starts the kernel in 32-bit protected mode (`platform/x64/boot.S` line 19)
- Provides a `struct hvm_start_info*` pointer in EBX register (line 24)
- May optionally provide RSDP address in the start info structure (`platform/x64/acpi.c` line 263)

### ACPI Table Discovery

ACPI tables are provided via QEMU's fw_cfg interface using I/O ports:
- **Port 0x510**: Selector port (write selector value)
- **Port 0x511**: Data port (read/write data)

Key fw_cfg files (`platform/x64/acpi.c` lines 15-22):
- `etc/acpi/rsdp`: Root System Description Pointer
- `etc/acpi/tables`: ACPI tables (MADT, MCFG, FADT, DSDT)

The ACPI implementation dynamically builds an RSDT from fw_cfg tables (line 166-197) and caches the DSDT for device discovery (line 149-152).

## Physical Memory Layout

### Low Memory (0x00000000 - 0x000FFFFF) - 1 MiB

```
0x00000000 - 0x00007FFF:  BIOS Data Area (32 KiB)
                          Reserved for firmware structures
0x00008000 - 0x0001FFFF:  Available low memory (96 KiB)
0x00020000 - 0x000FFFFF:  Extended BIOS Area (896 KiB)
```

### Kernel Memory (0x00200000+) - 2 MiB Base

The kernel is loaded at 2 MiB to avoid BIOS/firmware data structures, as required by the PVH boot protocol.

```
0x00200000:               Kernel base (.text.boot entry point)
                          Defined in platform/x64/linker.ld line 12

Section Layout (from linker.ld):
  .note.Xen               PVH boot note (lines 17-19)
  .text                   Code section (~49 KiB)
  .rodata                 Read-only data (page-aligned, ~7 KiB)
                          Contains GDT (boot.S lines 207-211)
  .data                   Initialized data (page-aligned)
  .bss                    Uninitialized data (page-aligned, ~187 KiB)
                          - IDT (4 KiB)
                          - g_kernel (~76 KiB)
                          - g_user (~14.5 KiB)
                          - stack (64 KiB, boot.S lines 223-225)
                          - page tables (24 KiB, see below)
```

### Page Tables (in BSS section)

Page tables are allocated in the BSS section after the stack (`platform/x64/linker.ld` lines 55-68):

```
_page_tables_start:       Base of page table region
  +0x0000 (4 KiB):        PML4 (Page Map Level 4)
                          - Covers 256TB address space
                          - PML4[0] -> PDPT (boot.S line 47)
  +0x1000 (4 KiB):        PDPT (Page Directory Pointer Table)
                          - Covers first 512GB
                          - PDPT[0-3] -> PD0-PD3 (lines 53-71)
  +0x2000 (4 KiB):        PD0 (Page Directory 0)
                          - Maps 0-1GB with 512 × 2MB pages (lines 73-84)
  +0x3000 (4 KiB):        PD1 (Page Directory 1)
                          - Maps 1-2GB with 512 × 2MB pages (lines 87-97)
  +0x4000 (4 KiB):        PD2 (Page Directory 2)
                          - Maps 2-3GB with 512 × 2MB pages (lines 99-110)
  +0x5000 (4 KiB):        PD3 (Page Directory 3)
                          - Maps 3-4GB with 512 × 2MB pages (lines 112-125)
                          - Uses PCD (Page Cache Disable) for MMIO regions
_page_tables_end:
```

**Page Table Flags** (`platform/x64/boot.S`):
- Normal memory (PD0-PD2): `0x83` = Present | Writable | Huge (line 80)
- MMIO region (PD3): `0x93` = Present | Writable | PCD | Huge (line 121)
  - PCD (Page Cache Disable) is critical for MSI-X interrupt delivery

## Memory-Mapped I/O Regions

### MMIO Mode (microvm, USE_PCI=0)

When running on QEMU microvm with VirtIO MMIO devices:

```
0xC0000000 - 0xCFFFFFFF:  PCI MMIO Reserved (256 MiB)
                          Defined but unused in MMIO mode
                          platform/x64/platform_config.h line 10

0xFE000000 - 0xFEFFFFFF:  High MMIO Region (16 MiB)
                          platform/x64/platform_config.h line 16

0xFEB00000 - 0xFEB0FFFF:  VirtIO MMIO Base (64 KiB)
                          platform/x64/platform_config.h line 22
                          Devices discovered via ACPI DSDT parsing
                          platform/x64/acpi.c line 554
                          - Searches for "LNRO0005" device IDs (line 464)
                          - Extracts MMIO base/size and IRQ from AML (lines 476-502)

0xFEC00000 - 0xFEC00FFF:  IOAPIC #0 (4 KiB)
                          Discovered from ACPI MADT
                          platform/x64/acpi.c line 106

0xFEC10000 - 0xFEC10FFF:  IOAPIC #1 (4 KiB)
                          Secondary IOAPIC if present

0xFEE00000 - 0xFEE00FFF:  Local APIC (4 KiB)
                          Discovered from ACPI MADT
                          MADT.local_apic_address (acpi.c line 299)
```

### PCI Mode (q35, USE_PCI=1)

When running on QEMU q35 with PCI VirtIO devices:

```
0xB0000000 - 0xBFFFFFFF:  PCI Configuration Space (ECAM)
                          256 MiB (16 buses × 32 devices × 8 functions × 4 KiB)
                          Discovered from ACPI MCFG table
                          Note: x64 uses I/O ports for PCI config access

0xC0000000 - 0xCFFFFFFF:  PCI BAR MMIO (256 MiB)
                          Mapped cache-disabled in PD3 (boot.S line 121)
                          BARs pre-assigned by QEMU firmware
                          Read via platform_pci_read_bar() (pci.c line 81)

0xFEB00000 - 0xFEB0FFFF:  VirtIO MMIO (unused in PCI mode)

0xFEC00000 - 0xFEC00FFF:  IOAPIC #0 (4 KiB)

0xFEE00000 - 0xFEE00FFF:  Local APIC (4 KiB)

0xFEE00000:               MSI-X Message Address (hardcoded)
                          x86 MSI/MSI-X always use 0xFEE00000 base
                          platform/x64/pci.c line 240
                          Format: 0xFEE00000 | (apic_id << 12)
```

### PCI Configuration Access

Unlike ARM platforms that use ECAM (memory-mapped), x64 uses I/O port-based PCI configuration:
- **Port 0xCF8**: Address port (write 32-bit address)
- **Port 0xCFC**: Data port (read/write 8/16/32-bit values)

Implementation in `platform/x64/pci.c` lines 10-78.

### MSI-X Configuration

MSI-X (Message Signaled Interrupts Extended) is configured for PCI devices:

1. **Find MSI-X Capability** (pci.c line 125)
   - Walk PCI capability list starting at offset 0x34
   - Search for capability ID 0x11 (MSI-X)

2. **Read MSI-X Table Location** (line 168)
   - Table BAR and offset from capability structure
   - Table contains 16-byte entries (address_low, address_high, data, control)

3. **Configure Interrupt Vector** (line 240)
   - Address: `0xFEE00000 | (apic_id << 12)` (hardcoded MSI address)
   - Data: `vector_number` (IRQ 33-47)
   - Control: 0 (unmask)

4. **Enable MSI-X** (line 301)
   - Set bit 15 (Enable) in MSI-X control register
   - Clear bit 14 (Function Mask) to unmask all vectors

## Device Discovery: Implementation Details and Concerns

### ACPI DSDT Bytecode Scanning

The x64 platform performs raw AML (ACPI Machine Language) bytecode scanning to discover VirtIO MMIO devices (`platform/x64/acpi.c` lines 450-509):

**Implementation**:
- Searches entire DSDT for "LNRO0005" string (VirtIO MMIO device ID, line 464)
- From match position, searches next 512 bytes for resource descriptors (lines 476-502):
  - `0x86` opcode: Memory32Fixed resource (base address and size, lines 479-488)
  - `0x89` opcode: Extended Interrupt descriptor (IRQ number, lines 493-501)
- Extracts MMIO base, size, and IRQ directly from raw bytecode

**Concerns**:
- **Fragile parsing**: Depends on QEMU's specific DSDT bytecode layout
  - Assumes resource descriptors appear within 512 bytes after "LNRO0005"
  - Assumes specific byte offsets within opcodes (e.g., base at `j+4`, IRQ at `j+5`)
- **Will break if**:
  - QEMU changes AML generation order or structure
  - Different firmware generates different DSDT layout
  - Resource descriptors use different encoding formats
- **Alternative approach**: Proper AML interpreter would be more robust but significantly more complex (hundreds of opcodes, control flow, method execution)
- **Current mitigation**: In PCI mode, firmware pre-assigns BARs so DSDT parsing is avoided entirely

### PCI Bus Scanning

The PCI device discovery code performs limited bus enumeration (`platform/shared/platform_virtio.c` lines 542-603):

**Implementation**:
- Scans only **4 buses** (0-3) × 32 slots = 128 configuration reads (line 552)
- Reads vendor/device ID at each slot (line 554-555)
- Filters for VirtIO vendor ID (0x1AF4) and device IDs (0x1000-0x107F)

**Concerns**:
- **Incomplete scanning**: "Scanning all 256 buses takes too long" (line 551)
- **May miss devices** on buses 4-255, though most QEMU configurations place all devices on bus 0
- **Performance trade-off**: Full scan = 8192 config reads vs. current 128 reads
- **Acceptable limitation**: QEMU typically doesn't use high-numbered buses in simple configurations
- **Future enhancement**: Could scan until N consecutive empty buses detected

## Address Discovery: Hardcoded vs Dynamic

The x64 platform uses a mix of hardcoded and ACPI-discovered addresses due to x86 architectural legacy:

| Region | Address | Discovery Method | Reference |
|--------|---------|------------------|-----------|
| Kernel Base | 0x00200000 | **Hardcoded** (PVH requirement) | linker.ld:12 |
| Page Tables | BSS section | **Dynamic** (linker allocation) | linker.ld:61 |
| PCI Config I/O | 0xCF8/0xCFC | **Hardcoded** (x86 standard) | pci.c:11-12 |
| PCI MMIO Range | 0xC0000000 | **Hardcoded** (platform convention) | platform_config.h:10 |
| VirtIO MMIO Base | 0xFEB00000 | **Hardcoded** (QEMU microvm default) | platform_config.h:22 |
| VirtIO MMIO Devices | Variable | **ACPI DSDT** (parse "LNRO0005") | acpi.c:464 |
| IOAPIC Address | Variable | **ACPI MADT** (I/O APIC entry) | acpi.c:324 |
| Local APIC Address | Variable | **ACPI MADT** (header field) | acpi.c:299 |
| PCI BARs | Variable | **Firmware-assigned** (read from config) | pci.c:81 |
| MSI-X Address | 0xFEE00000 | **Hardcoded** (x86 MSI convention) | pci.c:240 |

**Why More Hardcoded Values?**
- x86 has strong historical conventions (I/O ports, standard MMIO regions)
- Firmware typically pre-configures PCI BARs before kernel boot
- QEMU microvm uses known address ranges for simplicity
- ACPI provides device discovery but not all address assignments

Compare to ARM64 which relies more heavily on device tree for all address discovery.

### Concerns and Limitations

**MSI-X Address (0xFEE00000)**:
- **Actually correct**: This is an x86/x64 architectural requirement, not a QEMU-specific quirk
- All x86 MSI/MSI-X messages use the `0xFEE00000` base address (destination field at bits [19:12])
- Comment in `platform/x64/pci.c` lines 237-240 confirms: "x86 MSI/MSI-X always use 0xFEE00000 base, not actual LAPIC base"
- This is a hardware standard dating back to the original MSI specification
- **Status**: No portability concern - standard across all x86/x64 systems

**VirtIO MMIO Base (0xFEB00000)**:
- **QEMU microvm specific**: Only valid for the microvm machine type
- The q35 machine type uses PCI devices, not MMIO VirtIO devices
- Defined in `platform/x64/platform_config.h` line 22
- **Portability concern**: Won't work on non-microvm x86 platforms
- **Status**: Acceptable limitation - microvm is explicitly targeted configuration

**PCI MMIO Range (0xC0000000)**:
- **Hardcoded for QEMU q35**: Traditional x86 MMIO region placement
- Defined in `platform/x64/platform_config.h` line 10
- May not work on all x86 systems (e.g., physical hardware with different firmware)
- **Portability concern**: Assumes firmware assigns BARs in this range
- **Status**: Works for QEMU, may need adjustment for other platforms

**I/O Ports (0xCF8/0xCFC)**:
- **x86 architectural standard**: Not QEMU-specific, works on all x86 hardware
- Standard PCI configuration mechanism #1, defined in PCI specification
- Defined in `platform/x64/pci.c` lines 11-12
- **Status**: No portability concern - universal x86 standard

## Platform-Specific Concerns

### DSDT Parsing Fragility

**Most concerning issue for x64 platform**:
- ACPI DSDT parsing in `platform/x64/acpi.c` lines 450-509 depends on exact QEMU bytecode layout
- No proper AML (ACPI Machine Language) interpreter implemented
- Raw bytecode scanning assumes:
  - "LNRO0005" device ID appears verbatim in DSDT
  - Resource descriptors (opcodes 0x86, 0x89) appear within 512 bytes after device ID
  - Specific byte offsets for base address, size, and IRQ values

**Risk level**: High - will break if QEMU changes DSDT generation

**Alternatives**:
1. **Proper AML interpreter**: More robust but extremely complex
   - Requires implementing AML opcodes, method execution, namespace management
   - Several thousand lines of code (see ACPICA reference implementation)
2. **Rely on firmware BAR assignment**: Already done in PCI mode (USE_PCI=1)
   - Firmware pre-assigns BARs before kernel boot
   - Kernel reads assigned values via PCI configuration space
3. **Hardcode MMIO addresses**: Simple but inflexible
   - Works only for specific QEMU versions/configurations

**Current mitigation**: PCI mode (USE_PCI=1) avoids DSDT parsing entirely by using firmware-assigned BARs

### Two Machine Types

**microvm (USE_PCI=0)** vs **q35 (USE_PCI=1)** have different memory layouts:

| Feature | microvm | q35 |
|---------|---------|-----|
| VirtIO Transport | MMIO | PCI |
| Device Discovery | ACPI DSDT parsing | PCI bus scan |
| Hardcoded Addresses | 0xFEB00000 (MMIO base) | 0xC0000000 (PCI MMIO range) |
| Interrupt Delivery | IOAPIC | MSI-X |

**Code must handle both configurations**:
- Conditional compilation via `USE_PCI` macro
- Different device discovery paths (`platform/x64/acpi.c` vs `platform/shared/platform_virtio.c`)
- Different interrupt setup (IOAPIC vs MSI-X)

**Testing requirement**: Both configurations must be validated independently

### x86 Legacy and Architectural Standards

**More hardcoded values due to x86 architectural conventions**:

**Generally acceptable** (match x86 standards, not QEMU quirks):
- MSI-X address (0xFEE00000): x86 architectural requirement
- I/O port addresses (0xCF8/0xCFC): PCI configuration mechanism #1 standard
- Kernel base (0x00200000): PVH boot protocol requirement
- PCD flag for MMIO: Required for proper MSI-X delivery on x86

**QEMU-specific** (may not work elsewhere):
- VirtIO MMIO base (0xFEB00000): microvm machine type only
- PCI MMIO range (0xC0000000): Common but not universal
- ACPI tables via fw_cfg: QEMU-specific interface (ports 0x510/0x511)

**Recommendation**: For portability beyond QEMU, focus on PCI mode (USE_PCI=1) which relies more on standards

## Known Issues and Considerations

### Page Table Location
Page tables are in the BSS section (dynamic allocation) rather than hardcoded low memory addresses. This avoids potential conflicts with the Extended BIOS Data Area (EBDA), which may extend up to 0x9FFFF on some systems.

### Cache Control for MMIO
The PD3 page directory (3-4GB range) uses the PCD (Page Cache Disable) flag to ensure proper MSI-X interrupt delivery (`platform/x64/boot.S` line 121). Without cache-disable, MSI-X writes may be cached and delayed, causing interrupt delivery failures.

### PCI BAR Allocation
In PCI mode, the kernel relies on QEMU firmware to pre-assign BAR addresses. The kernel reads but does not allocate BARs. Custom BAR allocation would require:
1. Probing BAR size by writing 0xFFFFFFFF
2. Allocating address from PCI MMIO pool
3. Writing new BAR address to config space

### 32-bit vs 64-bit BARs
PCI devices may use 64-bit BARs (type code 0b10 in bits [2:1]). The `platform_pci_read_bar()` function handles both 32-bit and 64-bit BARs correctly (`platform/x64/pci.c` lines 110-120).

### ACPI DSDT Parsing (Legacy Note)
VirtIO MMIO device discovery parses raw AML (ACPI Machine Language) bytecode in the DSDT, searching for specific opcodes:
- `0x86`: Memory32Fixed resource descriptor (line 479)
- `0x89`: Extended Interrupt descriptor (line 493)

**Note**: See "Platform-Specific Concerns" section above for detailed analysis of DSDT parsing fragility and mitigation strategies.

## Debug Tools

### LLDB Scripts
Located in `platform/x64/script/debug/lldb_x86.txt`

### Memory Validation Functions
- `platform_mem_validate_critical()`: Check page tables, kernel sections
- `platform_mem_dump_pagetables()`: Walk PML4 → PDPT → PD → PT hierarchy

### Useful Commands
```bash
# Dump kernel ELF sections
llvm-objdump -h build/x64/kernel.elf

# Show symbol addresses
llvm-nm build/x64/kernel.elf | grep -E '(kernel_start|page_tables|stack)'

# Disassemble boot code
llvm-objdump -d build/x64/kernel.elf | less
```

### QEMU Monitor Commands
```bash
# View memory at physical address
(qemu) x/16xg 0x200000

# Dump page tables
(qemu) info mem

# Show APIC state
(qemu) info lapic
(qemu) info ioapic

# View PCI devices
(qemu) info pci
```

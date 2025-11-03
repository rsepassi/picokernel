# ARM64 Platform Memory Map

## Overview

The ARM64 platform runs on QEMU's `virt` machine, which provides a flexible virtual hardware environment. The kernel operates in identity-mapped physical address space (no MMU translation) at Exception Level 1 (EL1/supervisor mode). Memory layout is determined by a combination of hardcoded addresses (linker script), runtime discovery via Device Tree Blob (FDT), and build-time configuration (USE_PCI flag).

The platform uses a 64KB MMU granule with 48-bit virtual address space for page table setup, though the kernel itself runs with identity mapping.

## Boot Information

**Boot Sequence** (`platform/arm64/boot.S:11-39`):
1. QEMU loads kernel ELF at 0x40200000 (2 MB offset into RAM)
2. DTB (Device Tree Blob) placed at 0x40000000 by QEMU
3. Boot code sets up stack at `stack_top` (~0x40300000)
4. DTB pointer (0x40000000) loaded into x0 register
5. Control transfers to `kmain` with DTB pointer as argument

**Address Discovery** (`platform/arm64/platform_core.c:115-351`):
- FDT parsing discovers runtime addresses for GIC, UART, PCI ECAM, and VirtIO MMIO devices
- Memory regions discovered from FDT `memory@` nodes
- MMIO device addresses extracted from FDT `reg` properties
- PCI configuration uses FDT `ranges` property for BAR allocation

## Memory Layout

### Kernel Region (0x40000000 - 0x40400000)

```
0x40000000 - 0x401FFFFF:  Device Tree Blob (DTB) region (2 MiB reserved)
                          - QEMU places DTB here for ELF kernels
                          - Size aligned to 64KB pages (platform_core.c:130)
                          - Reserved from free memory (platform_core.c:716-724)

0x40200000:               Kernel base (linker.ld:15)
                          - .text.boot section (entry point _start)
                          - .text section (code)
                          - .rodata section (read-only data)
                          - .data section (initialized data)
                          - .bss section (uninitialized data):
                            * g_kernel (platform_t structure)
                            * g_user (user data)
                            * Stack: 64 KiB (boot.S:52, linker.ld:44)
                            * Page tables (L1, L2, L3 pools)

Stack:                    64 KiB allocated in .bss (boot.S:46-53)
                          - stack_bottom → stack_top
                          - Stack grows downward from stack_top
                          - Initial SP set in boot.S:24-25

_end:                     End of kernel (varies, ~240 KiB typical)
                          - Reserved from free memory (platform_core.c:726-739)
```

**Key Addresses** (all from `platform/arm64/linker.ld`):
- Kernel base: 0x40200000 (line 15)
- DTB region: 0x40000000 - 0x401FFFFF (lines 3-4, 13-14)
- Stack size: 65536 bytes (64 KiB) defined in boot.S:52

## MMIO Mode (USE_PCI=0)

When built with `USE_PCI=0`, VirtIO devices use memory-mapped I/O transport.

### VirtIO MMIO Devices

**Base Address**: 0x0A000000 (`platform/arm64/platform_impl.h:168`)

**Device Layout**:
```
0x0A000000:               VirtIO MMIO Device 0
0x0A000200:               VirtIO MMIO Device 1  (stride: 0x200 bytes, platform_impl.h:169)
0x0A000400:               VirtIO MMIO Device 2
...
0x0A003E00:               VirtIO MMIO Device 31 (max: 32 devices, platform_impl.h:170)
```

**Discovery Method** (`platform/arm64/platform_core.c:946-996`):
- Probes each 0x200-byte slot at MMIO base
- Checks for VirtIO magic value 0x74726976 ("virt") at offset 0x00
- Reads device ID at offset 0x08 to identify device type
- Non-zero device ID indicates valid device

**Interrupt Routing** (`platform/arm64/platform_impl.h:204-211`):
- MMIO devices use SPIs (Shared Peripheral Interrupts)
- Base SPI: 16
- IRQ number = 32 + (16 + device_index)
- Example: Device 0 → SPI 16 → GIC IRQ 48

### Generic Interrupt Controller (GIC)

**Addresses** (discovered from FDT, `platform/arm64/platform_core.c:203-214`):

```
0x08000000:               GIC Distributor (GICD)
                          - Size: 64 KiB (0x10000)
                          - Mapped explicitly (platform_core.c:581-586)

0x08010000:               GIC CPU Interface (GICC)
                          - Size: 64 KiB (0x10000)
                          - Mapped explicitly (platform_core.c:587-592)
```

### UART (PL011)

**Default Address**: 0x09000000 (`platform/arm64/uart.c:9`)
- Used until FDT parsing completes
- Actual address discovered from FDT (platform_core.c:193-200)
- Matched by compatible string "arm,pl011"
- Size: 4 KiB (0x1000)
- Mapped explicitly (platform_core.c:574-578)

## PCI Mode (USE_PCI=1)

When built with `USE_PCI=1`, VirtIO devices use PCI transport.

### PCI ECAM (Configuration Space)

**Base Address**: 0x4010000000 (`platform/arm64/platform_impl.h:165`)
- Extended Configuration Access Mechanism
- Discovered from FDT "pci-host-ecam-generic" node (platform_core.c:218-227)
- Size discovered from FDT `reg` property (typically 256 MiB per bus)
- Mapped via MMIO regions list (platform_core.c:561-572)

**Access Method**:
```
ECAM Address = ECAM_BASE + (bus << 20) + (device << 15) + (function << 12) + offset
```

### PCI MMIO Region (Device BARs)

**Allocation Strategy** (`platform/arm64/platform_core.c:228-261`):
- MMIO range for BAR allocation discovered from FDT `ranges` property
- PCI ranges format: (child-addr, parent-addr, size) tuples
- Searches for 64-bit MMIO space (flags 0x03000000 or 0x02000000)
- BAR allocator starts at `pci_mmio_base` (platform_core.c:335-339)
- Default fallback: 0x10000000 if FDT doesn't specify

**Typical Range** (from QEMU virt FDT):
```
0x10000000 - 0x3EFFFFFF:  PCI MMIO range for device BARs
                          - Size: ~750 MiB
                          - Dynamically allocated to PCI devices
                          - Mapped explicitly (platform_core.c:594-602)
```

**Interrupt Routing** (`platform/arm64/platform_impl.h:193-202`):
- PCI devices use legacy INTx interrupts
- Base SPI: 3
- Swizzling: `IRQ = 32 + (3 + ((slot + pin - 1) % 4))`
- IRQ pin read from PCI config space offset 0x3D (platform_core.c:1007-1008)

## Device Discovery: Implementation Details and Concerns

The kernel discovers VirtIO devices through two primary mechanisms: MMIO device probing and PCI bus scanning. While these approaches work reliably on QEMU virtual machines, they have important implementation details and potential concerns for other environments.

### MMIO Device Probing

**Mechanism** (`platform/arm64/platform_core.c:959-996`):
- Brute-force probing of 32 memory slots (0x200-byte intervals starting at 0x0A000000)
- Each slot is read to check for VirtIO magic value 0x74726976 ("virt" in little-endian)
- Device ID read at offset 0x08 to identify device type
- Non-zero device ID indicates valid device

**Concerns**:
- **Speculative Memory Reads**: Probes memory addresses that may not contain devices
- Reads are **safe on QEMU** but could cause bus errors on real hardware with strict MMU or memory-mapped peripherals
- No fault handling for invalid memory accesses
- Hardcoded base address (0x0A000000) is QEMU virt machine specific

**Reference**: `platform/arm64/platform_core.c:959-996`

### PCI Bus Scanning

**Mechanism** (`platform/shared/platform_virtio.c:542-603`):
- Scans 4 buses × 32 slots = 128 PCI configuration space reads
- Reads vendor/device ID at each slot (0xFFFFFFFF indicates no device)
- Identifies VirtIO devices by vendor ID (0x1AF4) and device ID range (0x1000-0x107F)

**Concerns**:
- **Limited Bus Range**: Only scans buses 0-3 (not all 256 possible buses)
- Comment at line 551 admits: "Scanning all 256 buses takes too long"
- **May miss devices** on buses 4+ in non-standard configurations
- Performance tradeoff: completeness vs. boot time

**Reference**: `platform/shared/platform_virtio.c:550-596`

## Hardcoded vs Discovered Addresses

| Component | Hardcoded | Discovered | Source | Notes |
|-----------|-----------|------------|--------|-------|
| **Kernel Base** | 0x40200000 | No | linker.ld:15 | Fixed to avoid DTB collision |
| **DTB Location** | 0x40000000 | No | boot.S:38 | QEMU places DTB here |
| **Stack** | In .bss | No | boot.S:46-53 | 64 KiB allocation |
| **VirtIO MMIO Base** | 0x0A000000 | Optional | platform_impl.h:168 | Can be overridden from FDT |
| **PCI ECAM Base** | 0x4010000000 | Yes | platform_impl.h:165 / platform_core.c:224-227 | Hardcoded default, validated from FDT |
| **UART** | 0x09000000 | Yes | uart.c:9 / platform_core.c:193-200 | Default until FDT parsed |
| **GIC Distributor** | No | Yes | platform_core.c:203-214 | From FDT "arm,gic-*" compatible |
| **GIC CPU Interface** | No | Yes | platform_core.c:203-214 | From FDT "arm,gic-*" compatible |
| **PCI MMIO Range** | 0x10000000 (fallback) | Yes | platform_core.c:228-261 | From FDT `ranges` property |
| **RAM Regions** | No | Yes | platform_core.c:164-189 | From FDT `memory@` nodes |
| **VirtIO Devices** | No | Yes | platform_core.c:946-996 | Probed at runtime (MMIO) or PCI scan |
| **PCI Device BARs** | No | Yes | Platform-specific | Allocated from PCI MMIO range |

### Hardcoded Address Concerns

Several critical addresses are hardcoded with QEMU virt machine assumptions:

**VirtIO MMIO Base (0x0A000000)**:
- **Issue**: QEMU virt machine specific, will not work on other virtual machines or real hardware
- **Dependency**: Relies on FDT to override for portability to other platforms
- **Risk**: Silent failure if FDT parsing fails and address is wrong for the platform
- **Reference**: `platform/arm64/platform_impl.h:168`

**PCI ECAM Base (0x4010000000)**:
- **Issue**: QEMU virt machine specific configuration space address
- **Mitigation**: Validated against FDT at runtime (`platform/arm64/platform_core.c:224-227`)
- **Risk**: Silent failure if FDT unavailable and hardcoded address is incorrect
- **Reference**: `platform/arm64/platform_impl.h:165`

**Limited Scan Ranges**:
- **MMIO**: Only 32 device slots scanned (`VIRTIO_MMIO_MAX_DEVICES` at `platform/arm64/platform_impl.h:170`)
- **PCI**: Only 4 buses scanned (not all 256 possible buses at `platform/shared/platform_virtio.c:552`)
- **Risk**: May miss devices in non-standard configurations or with different device layouts

**Impact**: These hardcoded values make the kernel tightly coupled to QEMU's virt machine. Porting to other virtual machines (e.g., Firecracker, Cloud Hypervisor) or real hardware would require updating these addresses or ensuring FDT provides correct overrides.

## MMU Configuration

**Page Table Setup** (`platform/arm64/platform_core.c:526-682`):
- Granule: 64 KB pages
- Virtual Address Space: 48-bit (256 TB)
- Translation Levels:
  - L1: 64 entries, each covers 4 TB (platform_impl.h:117)
  - L2: 8192 entries per table, each covers 512 MB (platform_impl.h:119-120)
  - L3: 8192 entries per table, each covers 64 KB (platform_impl.h:123-125)
- All tables aligned to 64 KB boundaries

**Memory Attributes**:
- Normal Memory: Inner/Outer Write-Back, Read/Write-Allocate (platform_core.c:624)
- Device Memory: Device-nGnRnE (non-Gathering, non-Reordering, no Early Write Ack) (platform_core.c:624)

**Mapped Regions** (identity-mapped):
1. All RAM regions from FDT (platform_core.c:605-615)
2. UART (platform_core.c:574-578)
3. GIC Distributor and CPU Interface (platform_core.c:581-592)
4. All MMIO regions from FDT (platform_core.c:561-572)
5. PCI MMIO range (platform_core.c:594-602)

## Memory Management

**Free Region Tracking** (`platform/arm64/platform_core.c:684-761`):
- Initial regions discovered from FDT
- Reserved regions subtracted:
  1. DTB region (0x40000000 - 0x401FFFFF)
  2. Kernel region (_start to _end)
- Maintains doubly-linked list of free regions
- Used for dynamic allocation via `platform_mem_regions()`

**Reserved Regions**:
1. **DTB**: 0x40000000 - 0x401FFFFF (2 MiB, aligned to 64 KB pages)
2. **Kernel**: 0x40200000 - _end (~240 KiB typical, includes stack and page tables)

## Debug and Validation

**Memory Validation** (`platform/arm64/platform_core.c:780-844`):
```c
platform_mem_validate_critical();  // Check kernel sections, DTB region
platform_mem_print_layout();       // Dump ARM64 memory map
platform_mem_debug_mmu();          // Verify MMU configuration
```

**LLDB Debugging**:
- Debug scripts: `platform/arm64/script/debug/lldb_arm64.txt`
- Register inspection:
```
(lldb) register read x0 x1 x2 x3    # General purpose registers
(lldb) register read x29 x30 sp pc  # Frame pointer, link, stack, PC
(lldb) register read CurrentEL      # Check exception level (should be 1)
```

**Memory Tools**:
```bash
# Examine kernel symbols and addresses
llvm-objdump -t build/arm64/kernel.elf | grep -E '(_start|_end|stack)'
llvm-nm build/arm64/kernel.elf | grep -E '(platform_t|kernel_t)'

# Disassemble specific sections
llvm-objdump -d build/arm64/kernel.elf | less
```

## Known Constraints

1. **Stack Size**: 64 KiB stack may be insufficient for deep call chains with large stack allocations
2. **DTB Collision**: Kernel must be loaded at 0x40200000 or higher to avoid overwriting DTB
3. **Identity Mapping**: No virtual memory translation (VA = PA), simplified but less flexible
4. **Single Platform**: Module-local pointer to platform in interrupt.c limits to one platform instance (acceptable for embedded, see platform/arm64/interrupt.c:52)
5. **Page Table Pool**: Fixed-size pools for L2/L3 tables (platform_impl.h:119-125), may exhaust on large memory systems
6. **MMIO Region Limit**: Maximum 32 VirtIO MMIO devices (platform_impl.h:170)
7. **Memory Region Limit**: Maximum memory regions defined by KCONFIG_MAX_MEM_REGIONS

## Mitigations and Design Rationale

The device discovery implementation balances practicality with portability concerns:

**FDT as Primary Mechanism**:
- Device Tree provides the canonical source for device addresses and capabilities
- Hardcoded values serve as fallbacks when FDT parsing is unavailable or incomplete
- Runtime validation compares discovered values against hardcoded defaults where possible
- Reference: `platform/arm64/platform_core.c:115-351`

**MMIO Probing Strategy**:
- Brute-force probing is **standard practice** for QEMU virt machine VirtIO MMIO discovery
- QEMU guarantees safe reads to unmapped memory (returns 0x00000000 or 0xFFFFFFFF)
- Alternative approaches (parsing FDT virtio-mmio nodes) are more complex and platform-specific
- Current implementation prioritizes simplicity for the target platform (QEMU)

**PCI Scanning Limitations**:
- 4-bus limit balances boot time vs. device discovery
- Most QEMU configurations place all devices on bus 0
- PCI hierarchy scanning (bridges, subordinate buses) not implemented for simplicity
- Acceptable tradeoff for a kernel study project targeting virtual machines

**Target Platform Reality**:
- VMOS is designed for **QEMU virtual machines**, not production hardware
- Hardcoded values reflect QEMU virt machine memory map conventions
- Real hardware deployment would require:
  - Complete FDT with all device nodes
  - Proper bus enumeration (PCI bridge scanning)
  - Fault-tolerant memory probing with MMU exception handling
  - Architecture-specific discovery mechanisms (ACPI on x86, FDT on ARM)

**Acceptable Risk Profile**:
- Current implementation is safe and correct for the target environment
- Documentation clearly identifies hardcoded assumptions and limitations
- Code structure allows for future enhancement without major refactoring
- Platform abstraction layer isolates hardware-specific discovery logic

## Platform-Specific Notes

**Exception Levels**:
- Kernel runs at EL1 (supervisor mode)
- Boot code validates EL1 execution (boot.S:16-21)
- No user mode (EL0) support currently

**Interrupt Handling**:
- Uses ARM Generic Interrupt Controller v2 (GICv2)
- Timer IRQ: PPI 14 → GIC IRQ 30 (interrupt.c:27)
- VirtIO MMIO IRQs: SPIs starting at 16 (platform_impl.h:209)
- PCI IRQs: SPIs starting at 3 with swizzling (platform_impl.h:199-201)

**Timer**:
- ARM Generic Timer (physical timer)
- Frequency read from CNTFRQ_EL0 register
- Uses EL1 physical timer interrupt (PPI 14)

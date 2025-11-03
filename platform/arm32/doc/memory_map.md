# ARM32 Platform Memory Map

## Overview

The ARM32 platform targets the QEMU `virt` machine with ARM Cortex-A15 CPU. This is a 32-bit architecture with a 4 GiB physical address space (32-bit addressing). The kernel runs entirely in physical memory without MMU-based virtual memory, using identity mapping for all memory accesses.

The memory layout is designed to avoid collisions between the device tree blob (DTB), kernel code/data, and memory-mapped I/O regions.

## Boot Information

At boot time, QEMU passes the device tree blob (DTB) location to the kernel:

- **DTB Location**: 0x40000000 (start of RAM)
- **DTB Passing Method**: Physical address loaded into register `r0` (see `platform/arm32/boot.S:43`)
- **Discovery Method**: Most device addresses (UART, GIC, PCI, VirtIO MMIO) are discovered by parsing the DTB at runtime

## Memory Layout

### Physical Memory Regions

```
0x40000000 - 0x401FFFFF:  Device Tree Blob (DTB) Region (2 MiB reserved)
                          - QEMU places DTB here for ELF kernels
                          - Kernel offset chosen to avoid collision
                          - See: platform/arm32/boot.S:4-5, linker.ld:4

0x40200000:               Kernel Base Address
                          - Defined in linker.ld:12
                          - .text.boot section (entry point _start)
                          - .text section
                          - .rodata section
                          - .data section
                          - .bss section (includes platform_t, globals)
                          - See: platform/arm32/linker.ld:9-40

After kernel sections:    Stack Region
                          - Supervisor mode stack: 16 KiB (boot.S:57-60)
                          - IRQ mode stack: 8 KiB (boot.S:62-65)
                          - Additional kernel stack: 64 KiB (linker.ld:42-46)
                          - Total ~88 KiB stacks

_end:                     End of kernel (varies by build)
                          - Defined in linker.ld:49-50
                          - Free memory available beyond this point
```

### Stack Layout Details

The ARM32 platform uses multiple stacks for different processor modes:

- **Supervisor Mode Stack**: 16 KiB (`.bss` section, `boot.S:57-60`)
  - Used during normal kernel execution
  - Stack pointer set in `boot.S:13`
- **IRQ Mode Stack**: 8 KiB (`.bss` section, `boot.S:62-65`)
  - Used during interrupt handling
  - Stack pointer set in `boot.S:22`
- **Kernel Stack**: 64 KiB (linker script allocation, `linker.ld:45`)
  - Additional stack space allocated after `.bss`
  - Total stack size: 0x10000 bytes (64 KiB)

## Device I/O Regions

The platform supports two transport modes for VirtIO devices: MMIO and PCI. The mode is selected at build time with the `USE_PCI` option.

### MMIO Mode (USE_PCI=0)

In MMIO mode, VirtIO devices are accessed via memory-mapped registers:

**VirtIO MMIO Configuration**:
- **Base Address**: 0x0A000000 (discovered from FDT or hardcoded fallback)
  - Defined in `platform/arm32/platform_impl.h:165`
  - Fallback used in `platform/arm32/platform_core.c:831-833`
- **Device Stride**: 0x200 bytes (512 bytes per device)
  - Defined in `platform/arm32/platform_impl.h:166`
- **Maximum Devices**: 32 devices
  - Defined in `platform/arm32/platform_impl.h:167`
- **Device Layout**:
  ```
  0x0A000000 - 0x0A0001FF: VirtIO MMIO Device 0
  0x0A000200 - 0x0A0003FF: VirtIO MMIO Device 1
  0x0A000400 - 0x0A0005FF: VirtIO MMIO Device 2
  ...
  0x0A003E00 - 0x0A003FFF: VirtIO MMIO Device 31
  ```
- **Discovery**: Devices probed by reading magic number (0x74726976 "virt")
  - See `platform/arm32/platform_impl.h:168`
- **IRQ Numbers**: SPI 16-47 (GIC IRQ 48-79)
  - Formula: `32 + (16 + device_index)` (`platform_impl.h:210-215`)

**UART (PL011)**:
- **Default Address**: 0x09000000 (hardcoded fallback)
  - Defined in `platform/arm32/uart.c:9`
- **Actual Address**: Discovered from FDT (`uart.c:43-46`)
- **Region Size**: 8 KiB
- **Registers Used**: DR (0x00), FR (0x18)

### PCI Mode (USE_PCI=1)

In PCI mode, VirtIO devices are accessed via PCI configuration space and BARs:

**PCI ECAM (Enhanced Configuration Access Mechanism)**:
- **Base Address**: 0x3F000000 (discovered from FDT)
  - Hardcoded fallback defined in `platform/arm32/platform_impl.h:162`
  - Runtime discovery in `platform/arm32/platform_core.c:230-236`
- **Configuration Space**: Standard PCIe ECAM layout
  - Bus 0-255, Device 0-31, Function 0-7
  - 4096 bytes per function
- **Discovery**: PCI bus enumeration reads vendor/device IDs

**BAR Allocation**:
- **BAR Base Address**: 0x10000000 (fallback if FDT doesn't specify PCI MMIO range)
  - Initialized in `platform/arm32/platform_init.c:25`
  - Updated from FDT in `platform/arm32/platform_core.c:327-328`
- **Allocation Strategy**: Sequential allocation for each device BAR
  - Managed via `platform->pci_next_bar_addr`
- **PCI MMIO Range**: Discovered from FDT `ranges` property
  - See `platform/arm32/platform_core.c:244-256`

**PCI IRQ Mapping**:
- **IRQ Swizzling**: Standard PCI INTx rotation
  - Base SPI: 3
  - Formula: `32 + (3 + ((slot + pin - 1) % 4))` (`platform_impl.h:200-207`)
  - Results in GIC IRQs 35-38 for PCI devices

## Device Discovery: Implementation Details and Concerns

### MMIO Device Probing

The MMIO device discovery mechanism in `platform/arm32/platform_core.c:838-896` performs **brute-force probing** of the VirtIO MMIO address space:

**Probe Strategy**:
- Scans 32 memory slots at 0x200-byte intervals starting at 0x0A000000
- Each slot is read to check for VirtIO magic value 0x74726976 ("virt")
- These are **speculative memory reads** to addresses that may not contain actual devices

**Safety Considerations**:
- Safe on QEMU virtual machines (reads to unmapped addresses return 0)
- Could cause **bus errors or exceptions on real hardware** if addresses are unmapped
- No fault handling or exception recovery implemented
- Relies on platform behavior where invalid reads are benign

**Implementation Reference**: `platform/arm32/platform_core.c:838-896`

**Code Flow**:
```c
// For each of 32 possible MMIO slots:
uint64_t base = mmio_base + (i * VIRTIO_MMIO_DEVICE_STRIDE);
uint32_t magic = *(volatile uint32_t *)base;  // Speculative read
if (magic == VIRTIO_MMIO_MAGIC) {
    // Device found at this address
}
```

### PCI Bus Scanning

The PCI device discovery mechanism in `platform/shared/platform_virtio.c:542-603` performs **limited PCI bus enumeration**:

**Scan Scope**:
- Scans 4 buses × 32 slots = **128 PCI configuration space reads**
- Limited to buses 0-3 (not all 256 buses)
- Only checks function 0 of each slot

**Limitations**:
- May **miss devices on higher-numbered buses** (buses 4-255)
- Does not scan multi-function devices (functions 1-7)
- Optimized for QEMU virt machine (which places devices on bus 0)

**Performance Trade-off**:
- Full scan of 256 buses would require 8192 configuration reads
- Current approach provides fast boot time at cost of completeness

**Implementation Reference**: `platform/shared/platform_virtio.c:550-596`

**Code Flow**:
```c
// Limited bus scan for performance
for (uint16_t bus = 0; bus < 4; bus++) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t vendor_device = pci_config_read32(...);
        // Check for VirtIO devices
    }
}
```

## GIC (Generic Interrupt Controller)

**Addresses** (discovered from FDT):
- **Distributor Base**: Stored in `platform->gic_dist_base`
  - Runtime discovery in `platform/arm32/platform_core.c:152-153`
- **CPU Interface Base**: Stored in `platform->gic_cpu_base`
  - Runtime discovery in `platform/arm32/platform_core.c:152-153`

**IRQ Number Space**:
- **0-31**: Internal interrupts (SGI/PPI)
- **32+**: Shared Peripheral Interrupts (SPI)
- **Platform IRQs**:
  - Generic Timer: PPI 30 (GIC IRQ 30)
  - VirtIO MMIO: SPI 16-47 (GIC IRQ 48-79)
  - PCI INTx: SPI 3-6 (GIC IRQ 35-38)

## Hardcoded vs Discovered Addresses

| Address/Range | Value | Discovery Method | Source Reference |
|---------------|-------|------------------|------------------|
| **DTB Base** | 0x40000000 | Hardcoded (QEMU convention) | `boot.S:43` |
| **Kernel Base** | 0x40200000 | Hardcoded (linker script) | `linker.ld:12` |
| **Supervisor Stack** | 16 KiB | Hardcoded (linker script) | `boot.S:57-60` |
| **IRQ Stack** | 8 KiB | Hardcoded (linker script) | `boot.S:62-65` |
| **Kernel Stack** | 64 KiB (0x10000) | Hardcoded (linker script) | `linker.ld:45` |
| **UART Base** | 0x09000000 | FDT (fallback) | `uart.c:9,43-46` |
| **VirtIO MMIO Base** | 0x0A000000 | FDT (fallback) | `platform_impl.h:165` |
| **VirtIO MMIO Stride** | 0x200 | Hardcoded | `platform_impl.h:166` |
| **PCI ECAM Base** | 0x3F000000 | FDT (fallback) | `platform_impl.h:162` |
| **BAR Allocation Base** | 0x10000000 | FDT or hardcoded | `platform_init.c:25` |
| **GIC Distributor** | (varies) | FDT only | `platform_core.c:152` |
| **GIC CPU Interface** | (varies) | FDT only | `platform_core.c:153` |

**Notes**:
- "FDT (fallback)" means the address is discovered from the device tree at runtime, with a hardcoded fallback if FDT parsing fails
- "FDT only" means the address must be discovered from the device tree; there is no hardcoded fallback
- "Hardcoded" means the address is fixed in source code or linker script

### Hardcoded Address Concerns

The following hardcoded addresses are **QEMU virt machine specific** and may not be portable to other ARM platforms:

**VirtIO MMIO Base (0x0A000000)**:
- QEMU Cortex-A15 virt machine convention
- **Will not work on other ARM platforms** without FDT override
- Used as fallback when FDT parsing fails or provides no MMIO base
- Reference: `platform/arm32/platform_impl.h:165`
- Fallback usage: `platform/arm32/platform_core.c:836-838`

**PCI ECAM Base (0x3F000000)**:
- QEMU virt machine specific (with `highmem=off` option)
- Used as fallback if FDT parsing fails to provide PCI host bridge address
- **Not portable to real hardware** or other virtual platforms
- Reference: `platform/arm32/platform_impl.h:162`
- Runtime discovery attempts: `platform/arm32/platform_core.c:230-236`

**BAR Allocation Base (0x10000000)**:
- Hardcoded starting address for PCI BAR allocations
- May **conflict with other memory regions** on non-QEMU platforms
- Updated from FDT PCI ranges if available, but defaults to this value
- Reference: `platform/arm32/platform_init.c:25`
- FDT update: `platform/arm32/platform_core.c:327-328`

**Fixed Stack Sizes**:
- Supervisor stack: 16 KiB (`boot.S:57-60`)
- IRQ stack: 8 KiB (`boot.S:62-65`)
- Kernel stack: 64 KiB (`linker.ld:42-46`)
- May be insufficient for complex applications or deep interrupt nesting
- Not adjustable without rebuilding kernel

**Limited Device Scan Ranges**:
- MMIO: Maximum 32 devices (`VIRTIO_MMIO_MAX_DEVICES` in `platform_impl.h:167`)
- PCI: Only 4 buses scanned (out of possible 256)
- Limits device discovery on platforms with more devices or different topology

## Boot Sequence Memory Operations

1. **QEMU Initialization** (`boot.S:4-5`)
   - QEMU places DTB at 0x40000000
   - QEMU loads kernel ELF starting at 0x40200000

2. **Boot Assembly** (`boot.S:10-51`)
   - Set Supervisor mode stack to `svc_stack_top` (~16 KiB)
   - Set IRQ mode stack to `irq_stack_top` (~8 KiB)
   - Clear `.bss` section (zeroes global variables)
   - Load DTB address (0x40000000) into `r0`
   - Branch to `kmain`

3. **Platform Initialization** (`platform_init.c:18-43`)
   - Parse FDT to discover device addresses
   - Initialize BAR allocator to 0x10000000
   - Initialize interrupt controller (GIC)
   - Initialize timer
   - Scan for VirtIO devices (PCI or MMIO)

4. **FDT Parsing** (`platform_core.c:150-328`)
   - Discover UART address from `compatible = "arm,pl011"`
   - Discover GIC addresses from `compatible = "arm,gic-400"`
   - Discover PCI ECAM from `compatible = "pci-host-ecam-generic"`
   - Discover PCI MMIO ranges from PCI node `ranges` property
   - Update UART base via `platform_uart_init()`

## Memory Constraints

**32-bit Address Space**: Maximum 4 GiB addressable memory
- Limits total RAM size
- All addresses must fit in 32 bits
- 64-bit MMIO operations split into two 32-bit accesses (`platform_impl.h:177-196`)

**No Virtual Memory**: Identity-mapped physical addresses
- No MMU setup or page tables (MMU code exists but is not enabled)
- All pointers are physical addresses
- No memory protection between kernel/user space

**Stack Limitations**: Fixed stack sizes
- Supervisor stack: 16 KiB (may overflow with deep call chains)
- IRQ stack: 8 KiB (must handle all interrupt nesting)
- Kernel stack: 64 KiB (additional allocation)

## Debug and Validation

**Memory Layout Inspection**:
```c
platform_mem_print_layout();  // Print discovered memory regions
```

**LLDB Memory Inspection**:
```
(lldb) x/32xw 0x40000000  # Examine DTB header
(lldb) x/32xw 0x40200000  # Examine kernel entry point
(lldb) register read r0   # Check DTB pointer passed to kmain
```

**Key Debugging Symbols**:
```
_start          # Entry point (0x40200000)
_text_start     # Start of code section
_bss_start      # Start of BSS (uninitialized data)
_end            # End of kernel image
stack_top       # Supervisor mode stack top
```

## Mitigations and Portability

### Design Philosophy

The ARM32 platform implementation prioritizes **QEMU virt machine compatibility** over general hardware portability. This design choice enables rapid development and testing while maintaining awareness of portability limitations.

### Primary Reliance on FDT

The platform uses **Flattened Device Tree (FDT)** as the primary device discovery mechanism:

**FDT-Based Discovery**:
- UART addresses discovered from `compatible = "arm,pl011"` nodes
- GIC addresses discovered from `compatible = "arm,gic-400"` nodes
- PCI ECAM base discovered from `compatible = "pci-host-ecam-generic"` nodes
- PCI MMIO ranges discovered from PCI host bridge `ranges` property

**Fallback Strategy**:
- Hardcoded addresses only used when FDT parsing fails
- Allows boot on QEMU even with incomplete or missing device tree
- Provides developer-friendly defaults for common QEMU configurations

### Target Platform Constraints

**QEMU virt Machine Assumptions**:
- VirtIO devices at known MMIO addresses (0x0A000000 base)
- PCI ECAM at 0x3F000000 (requires `highmem=off` QEMU option)
- Standard ARM Cortex-A15 interrupt routing
- Memory layout compatible with 32-bit addressing

**Not Designed For**:
- Production deployment on real ARM hardware
- Multiple ARM SoC variants (i.MX, OMAP, Exynos, etc.)
- Custom board designs with non-standard memory maps
- Platforms requiring MMU-based memory protection

### Real Hardware Deployment Requirements

For deployment on real ARM hardware, the following would be required:

**Complete and Accurate FDT**:
- All device addresses must be specified in device tree
- Cannot rely on hardcoded QEMU-specific fallbacks
- PCI ranges must match actual hardware topology
- Interrupt routing must match GIC configuration

**Address Space Validation**:
- Verify no conflicts between device regions and RAM
- Ensure BAR allocation ranges are properly reserved
- Validate stack sizes against application requirements

**Safe Device Probing**:
- Implement exception handling for bus errors
- Use FDT exclusively for device enumeration (no blind probing)
- Add bounds checking for all memory-mapped accesses

**Platform-Specific Adaptations**:
- Custom interrupt swizzling for specific SoC interrupt controllers
- Board-specific initialization sequences
- Proper clock and power management setup

### Portability Trade-offs

**Current Approach Benefits**:
- Fast boot time on QEMU (limited PCI bus scanning)
- Simple implementation without complex error handling
- Clear separation between discovered and hardcoded values
- Works reliably on target platform (QEMU virt)

**Portability Costs**:
- MMIO probing could cause crashes on real hardware
- Limited PCI bus scanning misses devices on higher buses
- Hardcoded addresses prevent running on non-QEMU platforms
- No dynamic stack sizing or memory layout adjustment

## References

- `platform/arm32/boot.S` - Boot assembly and stack setup
- `platform/arm32/linker.ld` - Memory layout and section placement
- `platform/arm32/platform_impl.h` - Platform constants and addresses
- `platform/arm32/platform_init.c` - Platform initialization sequence
- `platform/arm32/platform_core.c` - FDT parsing and address discovery
- `platform/arm32/uart.c` - UART driver and address handling
- QEMU `virt` machine documentation: https://www.qemu.org/docs/master/system/arm/virt.html

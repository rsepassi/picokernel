# RISC-V 64-bit Platform Memory Map

## Overview

VMOS runs on RISC-V 64-bit (RV64GC) architecture targeting the QEMU `virt` machine. The platform uses OpenSBI firmware for supervisor binary interface services and boots in Supervisor mode (S-mode). The kernel runs without MMU paging (identity-mapped when enabled) and interacts with VirtIO devices via either MMIO or PCI transport.

## Boot Information

**Firmware**: OpenSBI runs in Machine mode (M-mode) and provides:
- Early hardware initialization
- SBI interface for timer, console, and system control
- Boot context passing via registers

**Boot Protocol**:
- OpenSBI loads kernel at 0x80200000 and jumps to `_start` in S-mode
- Register `a0` contains pointer to Flattened Device Tree (FDT/DTB)
- Register `a1` contains boot HART ID

**Privilege Modes**:
- M-mode (Machine): OpenSBI firmware only
- S-mode (Supervisor): Kernel execution
- U-mode (User): Not used

**SBI Services**: Kernel makes ecalls to OpenSBI for:
- `sbi_set_timer`: Configure timer interrupts
- `sbi_console_putchar`: Debug UART output (fallback)
- `sbi_shutdown`: System halt/shutdown

## Memory Layout

### Physical RAM

```
0x80000000 - 0x801FFFFF:  OpenSBI Firmware (2 MiB reserved)
                          - Supervisor Binary Interface runtime
                          - Machine mode trap handlers
                          - Must not be touched by kernel

0x80200000:               Kernel Base (linker.ld:10)
                          - .text.boot (entry point _start)
                          - .text section (code)
                          - .rodata section (read-only data)
                          - .data section (initialized data)
                          - .bss section:
                            - g_kernel (platform_t structure)
                            - g_user (user state)
                            - Stack (64 KiB, grows downward)
                            - Page tables (if MMU enabled)

_end:                     End of kernel (linker symbol, linker.ld:45)
                          - Varies by build configuration
                          - Marks start of free memory

Dynamic:                  FDT location (passed in a0)
                          - Flattened Device Tree blob
                          - Discovered at boot, size from header
                          - Reserved from free memory pool
```

**Reference**: `platform/rv64/linker.ld:10` (kernel base address)

### Stack Configuration

- **Size**: 64 KiB (0x10000 bytes)
- **Location**: Allocated in .bss section
- **Growth**: Downward from `stack_top`
- **Symbols**: `stack_bottom`, `stack_top` (defined in boot.S)

## MMIO Mode (USE_PCI=0)

In MMIO mode, VirtIO devices are accessed via memory-mapped registers at fixed addresses. Discovery is performed by probing known address ranges and reading device magic values.

### VirtIO MMIO Layout

```
0x10001000:               VirtIO MMIO Base (platform_impl.h:146)
                          - First device at +0x0000 (0x10001000)
                          - Second device at +0x1000 (0x10002000)
                          - Third device at +0x1000 (0x10003000)
                          - ...
                          Device stride: 0x1000 bytes (4 KiB)
                          Max devices: 8
                          Magic value: 0x74726976 ("virt")
```

**Reference**: `platform/rv64/platform_impl.h:146` (VIRTIO_MMIO_BASE)
**Reference**: `platform/rv64/platform_impl.h:147` (VIRTIO_MMIO_DEVICE_STRIDE)

### Device Discovery

VirtIO MMIO devices are discovered by:
1. Reading base address from FDT if available (platform_core.c:266-278)
2. Falling back to hardcoded VIRTIO_MMIO_BASE (0x10001000)
3. Probing each slot (platform_core.c:825-858)
4. Reading magic value at offset 0x00
5. Reading device ID at offset 0x08

**Reference**: `platform/rv64/platform_core.c:814-861` (MMIO discovery)

### Interrupt Controller

```
0x0C000000:               PLIC Base (interrupt.c:30)
                          - Platform-Level Interrupt Controller
                          - Manages external device interrupts
                          - Context 0: M-mode hart 0
                          - Context 1: S-mode hart 0 (kernel uses this)
                          - Priority registers: base + 0x000000
                          - Pending registers: base + 0x001000
                          - Enable registers: base + 0x002080 (S-mode)
                          - Threshold register: base + 0x201000 (S-mode)
                          - Claim/complete: base + 0x201004 (S-mode)
```

**MMIO IRQ Assignment**:
- Device 0 (index 0): IRQ 1
- Device 1 (index 1): IRQ 2
- Device N (index N): IRQ N+1

**Reference**: `platform/rv64/interrupt.c:30` (PLIC base address)
**Reference**: `platform/rv64/platform_impl.h:182-187` (IRQ calculation)

### UART

```
0x10000000:               NS16550A UART (uart.c:10)
                          - 16550-compatible UART
                          - Used for platform_putc() output
                          - Address discovered from FDT
                          - Default fallback for early boot
```

**Reference**: `platform/rv64/uart.c:10` (UART base default)
**Reference**: `platform/rv64/platform_core.c:188-195` (FDT UART discovery)

## PCI Mode (USE_PCI=1)

In PCI mode, VirtIO devices are attached to the PCI bus and accessed via PCI configuration space (ECAM) and Base Address Registers (BARs).

### PCI Configuration Space

```
0x30000000:               PCI ECAM Base (platform_impl.h:143)
                          - Extended Configuration Access Mechanism
                          - 256 MB address space (typical)
                          - Discovered from FDT if available
                          - Address format: base + (bus << 20) + (dev << 15) + (func << 12) + reg
```

**Reference**: `platform/rv64/platform_impl.h:143` (PLATFORM_PCI_ECAM_BASE)
**Reference**: `platform/rv64/platform_core.c:219-227` (FDT ECAM discovery)

### PCI MMIO Space (BAR Allocation)

```
0x40000000:               PCI MMIO Base (default)
                          - Used for BAR allocation when not in FDT
                          - Actual base discovered from FDT "ranges" property
                          - Platform allocates BARs sequentially
                          - BAR sizes: typically 4 KiB - 16 KiB per device
```

**Default BAR Allocator**: If FDT does not specify PCI MMIO range, the platform uses 0x40000000 as the starting address for BAR allocation.

**Reference**: `platform/rv64/platform_core.c:353-356` (BAR allocator initialization)
**Reference**: `platform/rv64/platform_core.c:229-260` (FDT PCI MMIO range parsing)

### PCI Interrupt Routing

PCI devices use legacy INTx interrupts routed through the PLIC:

**IRQ Swizzling Formula**:
```
irq_number = 32 + ((slot + irq_pin - 1) % 4)
```

- Base IRQ: 32 (first PCI interrupt in PLIC)
- Rotated by device slot and interrupt pin
- Supports 4 shared interrupt lines (INTA, INTB, INTC, INTD)

**Reference**: `platform/rv64/platform_impl.h:174-179` (PCI IRQ swizzling)
**Reference**: `platform/rv64/platform_core.c:865-879` (PCI interrupt setup)

## Device Discovery: Implementation Details and Concerns

### MMIO Device Probing

The MMIO device discovery mechanism performs brute-force probing of memory addresses that may not contain actual devices. This approach has important implications for hardware portability:

**Probing Strategy** (`platform/rv64/platform_core.c:824-861`):
- Scans 8 memory slots at 0x1000-byte (4 KiB) intervals starting at 0x10001000
- Each slot is read to check for VirtIO magic value 0x74726976 ("virt")
- These are **speculative memory reads** to addresses that may be unmapped
- Maximum devices: 8 (`VIRTIO_MMIO_MAX_DEVICES` in `platform_impl.h:148`)

**Hardware Concerns**:
- **Safe on QEMU**: Virtual machine returns 0xFFFFFFFF for unmapped reads
- **Unsafe on real hardware**: May cause bus errors or exceptions on strict RISC-V implementations
- **Fewer slots than ARM**: Only 8 slots vs 32 on ARM platforms, but same fundamental issue
- **No MMU protection**: Kernel runs without MMU, so invalid accesses are not caught

**Reference**: `platform/rv64/platform_core.c:825-858` (MMIO probing loop)

### PCI Bus Scanning

PCI device discovery scans configuration space but limits the search range for performance:

**Scanning Strategy** (`platform/shared/platform_virtio.c:542-603`):
- Scans **4 buses × 32 slots = 128 PCI configuration space reads**
- Limited to buses 0-3 (comment: "Scanning all 256 buses takes too long")
- Each read accesses ECAM space to check vendor/device ID
- Returns 0xFFFFFFFF if no device present at that slot

**Limitations**:
- **May miss devices**: Devices on buses 4+ will not be discovered
- **Performance trade-off**: Full 256-bus scan is too slow for boot time
- **QEMU compatibility**: QEMU typically places devices on bus 0

**Reference**: `platform/shared/platform_virtio.c:550-596` (PCI scanning loop)

## Hardcoded Address Concerns

The platform relies on several hardcoded addresses that are specific to the QEMU RISC-V virt machine. These addresses will not work on other RISC-V hardware platforms without modification or FDT override.

### VirtIO MMIO Base (0x10001000)

**Source**: `platform/rv64/platform_impl.h:146`
```c
#define VIRTIO_MMIO_BASE 0x10001000ULL
```

**Concerns**:
- QEMU RISC-V virt machine specific layout
- Will not work on SiFive boards, other RISC-V platforms, or custom hardware
- Requires FDT to specify correct `virtio_mmio@...` nodes for portability
- Fallback value only, but critical if FDT unavailable or malformed

### PCI ECAM Base (0x30000000)

**Source**: `platform/rv64/platform_impl.h:143`
```c
#define PLATFORM_PCI_ECAM_BASE 0x30000000ULL
```

**Concerns**:
- Hardcoded fallback if FDT unavailable
- Different from ARM/x64 ECAM addresses (each platform has its own convention)
- QEMU virt machine specific
- FDT discovery is primary mechanism (see `platform/rv64/platform_core.c:219-227`)

### PCI MMIO Allocation Base (0x40000000)

**Source**: `platform/rv64/platform_core.c:356`
```c
platform->pci_next_bar_addr =
    platform->pci_mmio_base ? platform->pci_mmio_base : 0x40000000;
```

**Concerns**:
- Used when FDT doesn't specify PCI MMIO range via "ranges" property
- May conflict with RAM or other memory regions on non-QEMU systems
- BAR allocator proceeds sequentially from this address
- Proper FDT required for production use

**Reference**: `platform/rv64/platform_core.c:353-356` (BAR allocator initialization)

### PLIC Base (0x0C000000)

**Source**: `platform/rv64/interrupt.c:30`
```c
static uint64_t g_plic_base = 0x0C000000ULL;
```

**Concerns**:
- QEMU default address
- Override available via FDT parsing (primary discovery method)
- Critical for interrupt routing; wrong address breaks all device interrupts

**Reference**: `platform/rv64/platform_core.c:198-205` (FDT PLIC discovery)

### UART Base (0x10000000)

**Source**: `platform/rv64/uart.c:10`
```c
static volatile uintptr_t g_uart_base = 0x10000000UL;
```

**Concerns**:
- NS16550A at QEMU default address
- Used for early boot before FDT parsing
- Override available via FDT (primary discovery method)

**Reference**: `platform/rv64/platform_core.c:188-195` (FDT UART discovery)

### Limited Scan Ranges

**MMIO Devices**:
- Only 8 device slots (`VIRTIO_MMIO_MAX_DEVICES`)
- May miss devices in larger configurations (though rare in practice)

**PCI Buses**:
- Only buses 0-3 scanned (4 buses total)
- Devices on buses 4+ will not be discovered
- Trade-off between boot time and device coverage

## RISC-V Platform Considerations

### OpenSBI Dependency

VMOS requires OpenSBI firmware to provide:
- **Correct FDT**: Primary device discovery mechanism; hardcoded values are emergency fallbacks
- **M-mode initialization**: OpenSBI sets up machine mode before transferring control
- **SBI interface**: Timer, console, and shutdown services

**Implication**: Kernel cannot boot standalone; OpenSBI is mandatory for proper initialization.

### Memory Model

RISC-V uses a weakly-ordered memory model requiring explicit synchronization:

**FENCE Instructions**: Used in MMIO operations to ensure proper ordering
```c
static inline void platform_mmio_barrier(void) {
  __asm__ volatile("fence iorw, iorw" ::: "memory");
}
```

**Reference**: `platform/rv64/platform_impl.h:152-155` (MMIO barrier implementation)

**Implication**: MMIO probing includes proper memory barriers to prevent reordering of device access.

### Target Platform

**Designed for**: QEMU `virt` machine
- Standard QEMU memory layout
- Known device addresses
- Predictable FDT structure

**Not designed for**:
- SiFive boards (HiFive Unmatched, etc.)
- Other vendor RISC-V hardware
- Custom RISC-V implementations

**Portability requirement**: Complete and accurate FDT required for non-QEMU platforms.

### Sv39 MMU (Optional)

The platform supports optional Sv39 MMU setup:
- **Identity mapping**: Virtual addresses = Physical addresses
- **Memory protection**: Could catch invalid MMIO probes if enabled
- **Current mode**: Runs without MMU by default

**Implication**: MMU setup would provide protection against bad MMIO probes by triggering page faults on unmapped regions, but current implementation runs with MMU disabled (identity mapping).

**Reference**: `platform/rv64/platform_core.c:494-615` (MMU setup implementation)

## Hardcoded vs Discovered Addresses

| Address/Region | Source | Reference | Notes |
|---------------|--------|-----------|-------|
| **Kernel Base** | **Hardcoded** | linker.ld:10 | 0x80200000 (fixed in linker script) |
| **OpenSBI Region** | **Hardcoded** | N/A | 0x80000000-0x801FFFFF (RISC-V convention) |
| **UART Base** | **FDT-discovered** | platform_core.c:188-195 | Fallback: 0x10000000 (uart.c:10) |
| **PLIC Base** | **FDT-discovered** | platform_core.c:198-205 | Fallback: 0x0C000000 (interrupt.c:30) |
| **CLINT Base** | **FDT-discovered** | platform_core.c:208-216 | Core-Local Interruptor |
| **VirtIO MMIO** | **FDT-discovered** | platform_core.c:266-278 | Fallback: 0x10001000 (platform_impl.h:146) |
| **PCI ECAM** | **FDT-discovered** | platform_core.c:219-227 | Fallback: 0x30000000 (platform_impl.h:143) |
| **PCI MMIO Range** | **FDT-discovered** | platform_core.c:229-260 | Fallback: 0x40000000 (platform_core.c:356) |
| **RAM Regions** | **FDT-discovered** | platform_core.c:159-184 | Multiple regions possible |
| **FDT Location** | **Boot register** | boot.S (a0 register) | Passed by OpenSBI at boot |

## FDT (Flattened Device Tree)

The device tree is the primary source of platform configuration information:

**Discovery**: Passed in register `a0` by OpenSBI at boot
**Parsing**: Single-pass traversal in `platform_boot_context_parse()` (platform_core.c:108-368)
**Information Extracted**:
- RAM regions (memory@ nodes)
- Device base addresses (UART, PLIC, CLINT, PCI, VirtIO)
- PCI configuration (ECAM base, MMIO ranges)
- MMIO device regions (for MMU mapping)

**Memory Reservation**: The FDT itself is reserved from the free memory pool to prevent overwriting.

**Reference**: `platform/rv64/platform_core.c:108-368` (FDT parsing implementation)

## MMU Configuration (Sv39)

VMOS supports optional MMU setup using RISC-V Sv39 paging (3-level page tables, 4 KiB pages, 39-bit virtual address space).

**Page Table Levels**:
- L2 (root): 512 entries, each covers 1 GB
- L1: 512 entries per L2, each covers 2 MB (megapages)
- L0 (leaf): Not used (2 MB megapages for efficiency)

**Mapping Strategy**:
- Identity mapping (virtual = physical)
- RAM regions: Readable, writable, executable (PTE_RAM)
- MMIO regions: Readable, writable, no execute (PTE_MMIO)
- 2 MB megapages for all mappings

**satp Register**: Set to Sv39 mode with page table root physical page number

**Reference**: `platform/rv64/platform_core.c:494-615` (MMU setup)

## Memory Management

**Free Memory Regions**: Discovered from FDT, then reserved regions subtracted:
1. OpenSBI firmware (0x80000000-0x801FFFFF)
2. Kernel image (_text_start to _end)
3. FDT blob (location from a0 register)

**Allocator**: Free regions maintained as doubly-linked list for O(1) traversal

**Reference**: `platform/rv64/platform_core.c:618-692` (free region list building)

## Debug Information

**LLDB Scripts**: `platform/rv64/script/debug/lldb_rv64.txt`

**Important Registers**:
```
a0-a7    - Function arguments / temporaries
t0-t6    - Temporary registers
sp       - Stack pointer
ra       - Return address
pc       - Program counter
sstatus  - Supervisor status (interrupt enable, etc.)
sie      - Supervisor interrupt enable
sip      - Supervisor interrupt pending
stvec    - Supervisor trap vector
sepc     - Supervisor exception PC
scause   - Supervisor exception cause
```

**Memory Validation**:
```c
platform_mem_validate_critical();  // Check critical regions
platform_mem_print_layout();        // Print memory map
```

## Key Details

**Device Tree Format**: Flattened Device Tree (FDT/DTB) binary blob
**Boot Context**: FDT pointer in a0, boot HART ID in a1
**Interrupt Model**: PLIC for external interrupts, CLINT for timer/IPI
**Timer**: SBI timer interface, nanosecond precision via timebase frequency
**Stack Size**: 64 KiB (sufficient for kernel + interrupt context)
**VirtIO Transport**: MMIO or PCI (compile-time selection via USE_PCI)
**Maximum IRQs**: 128 (MAX_IRQS, platform_impl.h:33)

## Known Issues

**OpenSBI Dependency**: Kernel cannot boot standalone; requires OpenSBI firmware to initialize M-mode and provide SBI interface.

**PLIC Complexity**: PLIC context selection (M-mode vs S-mode) requires correct register offsets. Kernel must use S-mode context 1 registers.

**Device Probing**: MMIO devices require probing known address ranges. PCI provides enumeration but requires ECAM and BAR configuration.

**FDT Parsing**: Single-pass traversal assumes well-formed device tree. Invalid DTB causes initialization failure.

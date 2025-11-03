# RISC-V 32-bit Platform Memory Map

## Overview

The RV32 platform targets the QEMU `virt` machine running a RISC-V 32-bit processor. The kernel runs in Supervisor mode (S-mode) on top of OpenSBI firmware, which provides the Supervisor Binary Interface (SBI) for accessing machine-mode functionality like timers and system shutdown.

## Boot Information

**Boot Protocol**: OpenSBI firmware launches the kernel in S-mode with:
- `a0` register: Hardware thread ID (hartid) - passed through boot.S
- `a1` register: Pointer to Flattened Device Tree (FDT) blob - passed to `kmain` (`platform/rv32/boot.S:16`)

**SBI Interface**: Provides machine-mode services:
- Timer management (`sbi_set_timer`, `sbi_get_time`)
- Console output (fallback debug)
- System shutdown (`sbi_shutdown`)

**Privilege Modes**:
- M-mode: OpenSBI firmware only
- S-mode: VMOS kernel
- U-mode: Not used

## Memory Layout

### RAM Region (0x80000000+)

```
0x80000000 - 0x801FFFFF:  OpenSBI firmware region (2 MiB reserved)
                          - Supervisor Binary Interface (SBI)
                          - Machine-mode runtime
                          - Kernel offset to avoid collision

0x80200000:               Kernel base (platform/rv32/linker.ld:10)
                          - .text.boot (entry point: _start)
                          - .text section (code)
                          - .rodata section (constants)
                          - .data section (initialized data)
                          - .bss section (zero-initialized):
                            - g_kernel (platform_t instance)
                            - g_user
                            - Stack: 64 KiB / 0x10000 bytes (linker.ld:41)

End of kernel (_end):     Varies by build (aligned to 16 bytes)
```

**Stack Configuration**:
- Size: 64 KiB (0x10000 bytes) - `platform/rv32/linker.ld:41`
- Growth: Downward from `stack_top`
- Alignment: 16-byte aligned (`linker.ld:39`)

### MMIO Mode Memory Map (USE_PCI=0)

MMIO device addresses are discovered via the Flattened Device Tree (FDT) at boot.

```
0x02000000:               CLINT (Core Local Interruptor)
                          - Timer interrupts (mtime, mtimecmp)
                          - Software interrupts (IPI)
                          - Accessed via SBI (not directly by kernel)

0x0C000000:               PLIC (Platform-Level Interrupt Controller)
                          - Priority registers: 0x0C000000
                          - Pending bitmap: 0x0C001000
                          - Enable (S-mode context 1): 0x0C002080
                          - Threshold (S-mode): 0x0C201000
                          - Claim/Complete (S-mode): 0x0C201004
                          - Supports up to 128 IRQs
                          - Reference: platform/rv32/interrupt.c:36-41

0x10000000:               NS16550A UART
                          - Debug output via platform_putc()
                          - 8 registers at 1-byte offsets
                          - Reference: platform/rv32/uart.c:7

0x10001000:               VirtIO MMIO base (device 0)
                          - Device stride: 0x1000 bytes (4 KiB)
                          - Max devices: 8
                          - Magic value: 0x74726976 ("virt")
                          - Discovery: FDT + magic probe
                          - IRQ assignment: base 1 + device index
                          - References:
                            - platform/rv32/platform_impl.h:128-130
                            - platform/rv32/platform_init.c:140-188
```

**MMIO IRQ Mapping**:
- VirtIO MMIO device 0 → IRQ 1
- VirtIO MMIO device 1 → IRQ 2
- VirtIO MMIO device N → IRQ (1 + N)
- Calculation: `platform/rv32/platform_impl.h:174-178`

### PCI Mode Memory Map (USE_PCI=1)

PCI configuration is discovered at boot, but base addresses are hardcoded.

```
0x30000000:               PCI ECAM (Enhanced Configuration Access Mechanism)
                          - Configuration space for PCI devices
                          - Size varies (parsed from FDT)
                          - Reference: platform/rv32/platform_impl.h:125

0x40000000:               PCI MMIO BAR allocation base
                          - Dynamically assigned to PCI device BARs
                          - Allocated sequentially during device enumeration
                          - Reference: platform/rv32/platform_init.c:36-37
```

**PCI IRQ Mapping** (legacy INTx):
- Base IRQ: 32
- Swizzle formula: `32 + ((slot + irq_pin - 1) % 4)`
- Calculation: `platform/rv32/platform_impl.h:165-170`
- Configuration: `platform/rv32/platform_init.c:192-206`

## Address Discovery Methods

| Address | Value | Method | Source File |
|---------|-------|--------|-------------|
| **RAM** |
| Kernel base | 0x80200000 | Hardcoded in linker script | `linker.ld:10` |
| Stack size | 64 KiB (0x10000) | Hardcoded in linker script | `linker.ld:41` |
| **MMIO Mode** |
| UART | 0x10000000 | Hardcoded | `uart.c:7` |
| PLIC | 0x0C000000 | Hardcoded | `interrupt.c:36` |
| CLINT | 0x02000000 | N/A (accessed via SBI) | Timer uses SBI calls |
| VirtIO MMIO base | 0x10001000 | Hardcoded fallback, FDT preferred | `platform_impl.h:128`, `platform_init.c:144-147` |
| VirtIO device stride | 0x1000 | Hardcoded | `platform_impl.h:129` |
| VirtIO IRQ | 1 + device_index | Calculated | `platform_impl.h:174-178` |
| **PCI Mode** |
| PCI ECAM base | 0x30000000 | Hardcoded | `platform_impl.h:125` |
| PCI BAR allocator | 0x40000000 | Hardcoded | `platform_init.c:37` |
| PCI IRQ | 32 + swizzle | Calculated | `platform_impl.h:165-170` |
| **Runtime** |
| Timer frequency | ~10 MHz | FDT (timebase-frequency) | `timer.c:146-180` |

## Addressing and Memory Model

**Address Space**: 32-bit physical addresses (max 4 GiB)

**Memory Model**: Sv32 (2-level page tables), but MMU is **not used** in VMOS
- Kernel runs with identity mapping
- All memory accesses are physical

**MMIO Ordering**: Weakly-ordered memory model requires explicit barriers
- `fence iorw, iorw` instruction ensures proper MMIO ordering
- Used before/after all MMIO reads/writes
- Reference: `platform/rv32/platform_impl.h:136-138`

**64-bit MMIO on RV32**: Split into two 32-bit operations
- Reads: low word first, then high word
- Writes: low word first, then high word
- References: `platform/rv32/platform_impl.h:141-160`

## Interrupt Configuration

**Interrupt Controller**: PLIC (Platform-Level Interrupt Controller)
- Base: 0x0C000000
- Max IRQs: 128
- S-mode context: 1 (context 0 is M-mode)

**Interrupt Sources**:
- Timer: Supervisor timer interrupt (via SBI)
- External: PLIC (VirtIO devices, UART, PCI)
- Software: Not used

**CSR Registers** (Supervisor mode):
- `sstatus`: Supervisor status (SIE bit for global enable)
- `sie`: Supervisor interrupt enable (STIE, SEIE bits)
- `stvec`: Trap vector base address
- `scause`: Cause of trap/interrupt
- `sepc`: Exception program counter

**Initialization Sequence**:
1. Set trap vector (`interrupt.c:139`)
2. Initialize PLIC priorities and threshold (`interrupt.c:109-120`)
3. Enable timer and external interrupts in SIE (`interrupt.c:149`)
4. Interrupts remain globally disabled during device enumeration
5. Global enable via SSTATUS.SIE after platform_init completes

## Device Discovery: Implementation Details and Concerns

### MMIO Device Probing

The kernel performs **brute-force probing** of VirtIO MMIO device slots to discover hardware devices. This approach has important implications for portability and safety:

**Implementation Details**:
- Probes 8 memory slots at 0x1000-byte intervals starting at 0x10001000
- Each slot is read to check for VirtIO magic value 0x74726976 ("virt" in little-endian)
- Device ID read at offset 0x08 to identify device type
- Reference: `platform/rv32/platform_init.c:148-188`

**Safety Concerns**:
- **Speculative Memory Reads**: Accesses addresses that may not contain actual devices
- **Safe on QEMU**: Virtual machine tolerates reads to unmapped MMIO regions
- **Unsafe on Real Hardware**: Could cause bus errors, exceptions, or system hangs on physical RISC-V systems
- **No Protection**: Running without MMU means no memory access protection

**Limitations**:
- **Limited Scan Range**: Only 8 slots scanned (vs 32 on ARM platforms)
- **Hardcoded Addresses**: Starting address 0x10001000 is QEMU-specific
- **No FDT Fallback**: If FDT parsing fails, relies entirely on hardcoded probing

### PCI Bus Scanning

The kernel scans PCI configuration space to discover PCI-attached VirtIO devices, but with performance-driven limitations:

**Implementation Details**:
- Scans 4 buses × 32 slots = 128 PCI configuration space reads
- Limited to 4 buses (out of possible 256) for performance reasons
- Comment in code: "Scanning all 256 buses takes too long"
- Reference: `platform/shared/platform_virtio.c:550-596`

**Limitations**:
- **Incomplete Discovery**: May miss devices on buses 4-255
- **Assumes Device Placement**: Relies on QEMU placing all devices on buses 0-3
- **No Dynamic Adjustment**: Bus count is hardcoded, not configurable

**Trade-offs**:
- Faster boot times vs. complete device discovery
- Acceptable for QEMU virt machine (devices on bus 0)
- Problematic for real hardware with complex PCI topologies

## Hardcoded Address Concerns and Limitations

### QEMU-Specific MMIO Addresses

**Critical**: All MMIO base addresses are hardcoded for the QEMU RISC-V `virt` machine and will **not work** on other RISC-V hardware:

| Device | Address | Reference | Portability |
|--------|---------|-----------|-------------|
| VirtIO MMIO | 0x10001000 | `platform_impl.h:128` | QEMU virt only |
| UART (NS16550A) | 0x10000000 | `uart.c:7` | QEMU virt only |
| PLIC | 0x0C000000 | `interrupt.c:36` | QEMU virt only |
| CLINT | 0x02000000 | N/A (SBI access) | QEMU virt only |

**Impact on Portability**:
- **SiFive Boards**: Different memory maps, would require code changes
- **Other RISC-V Platforms**: Incompatible without FDT-based discovery
- **Real Hardware**: May conflict with actual device placements

### PCI Configuration Addresses

**PCI ECAM Base**: 0x30000000
- Hardcoded for QEMU virt machine
- Reference: `platform_impl.h:125`
- Real hardware may place ECAM elsewhere

**PCI BAR Allocator**: 0x40000000
- Starting address for dynamic BAR assignment
- Reference: `platform_init.c:37`
- Could conflict with RAM on systems with different memory layouts
- No validation against existing memory regions

**Collision Risks**:
- If a platform has RAM at 0x40000000+, BAR allocation would corrupt memory
- No bounds checking or conflict detection
- Assumes QEMU memory layout (RAM at 0x80000000+)

### 32-bit Address Space Limitations

**Maximum Addressable Memory**: 4 GiB (2³² bytes)

**Constraints**:
- All MMIO regions must fit below 4 GiB
- No high memory (>4GB) available for large PCI BARs
- Large device BARs could exhaust address space
- 64-bit PCI BARs cannot be used at their full capacity

**MMIO Operation Workarounds**:
- 64-bit MMIO reads split into two 32-bit operations (read low word, then high)
- 64-bit MMIO writes split into two 32-bit operations (write low word, then high)
- Reference: `platform_impl.h:141-160`
- Requires careful ordering and memory barriers

### Limited Scan Ranges

**MMIO Devices**: Maximum 8 devices
- Defined by `VIRTIO_MMIO_MAX_DEVICES`
- Hardcoded in `platform_impl.h:130`
- Cannot discover more devices even if present

**PCI Buses**: Only 4 buses scanned
- Out of 256 possible buses
- Performance optimization for QEMU
- May miss devices on real hardware with complex topologies

## RV32-Specific Considerations

### 32-bit Architectural Constraints

**Tighter Address Space than RV64**:
- All MMIO regions must be below 4 GiB
- Large PCI BARs (e.g., GPU frame buffers) could exhaust address space
- Cannot utilize high memory regions available on 64-bit platforms
- Address space fragmentation more critical than on RV64

**64-bit Value Handling**:
- All 64-bit MMIO operations require splitting
- Potential atomicity issues if device updates between reads/writes
- Memory barriers required to ensure correct ordering
- Reference: `platform_impl.h:141-160`

### OpenSBI Firmware Dependency

**FDT Provision**:
- Requires accurate Flattened Device Tree from OpenSBI
- FDT provides primary device discovery mechanism
- Hardcoded addresses serve as fallbacks for QEMU compatibility
- Incomplete FDT parsing limits portability (only timer frequency extracted)

**Machine-Mode Access**:
- Cannot directly access M-mode resources (CLINT, M-mode CSRs)
- Must use SBI calls for timer management (`sbi_set_timer`, `sbi_get_time`)
- System shutdown requires SBI (`sbi_shutdown`)
- Reference: `platform/rv32/sbi.h`, `platform/rv32/timer.c`

### No MMU Operation

**Identity-Mapped Physical Addressing**:
- Kernel runs with all virtual addresses = physical addresses
- No memory protection between kernel components
- No guard pages for stack overflow detection

**Memory Access Risks**:
- Speculative MMIO reads have no protection mechanism
- Invalid addresses could hang system or cause bus errors
- No page fault handling to detect bad pointers
- Bugs manifest as immediate crashes rather than catchable faults

### Target Platform Specificity

**Designed for QEMU `virt` Machine**:
- Memory map specifically matches QEMU implementation
- Device addresses hardcoded for this platform
- Boot protocol assumes OpenSBI firmware launcher

**Porting to Real Hardware Would Require**:
- Complete FDT parsing for device discovery
- Dynamic MMIO region detection
- Platform-specific device drivers (different UART, interrupt controllers)
- Memory layout validation and conflict detection
- BAR allocator with proper bounds checking

### Debug Infrastructure Awareness

**Memory Debugging Tools**: `platform/rv32/platform_mem_debug.c:207-325`
- Validates access to critical MMIO regions (UART, PLIC, VirtIO)
- Checks timer initialization
- Reports discovered devices
- Re-verifies memory checksums after initialization

**Indicates Awareness of Concerns**:
- Explicit validation suggests known fragility
- Debug tools compensate for lack of hardware protection
- Shows developers anticipated memory mapping issues

## Build Configuration

**Target Triple**: `riscv32-unknown-elf`

**QEMU Machine**: `virt`
- CPU: `rv32` (32-bit RISC-V)
- RAM: 128 MiB default
- Devices: VirtIO (MMIO or PCI), UART, PLIC, CLINT

**Build Options**:
- `PLATFORM=rv32`: Select RV32 platform
- `USE_PCI=0`: VirtIO MMIO transport (default)
- `USE_PCI=1`: VirtIO PCI transport

## Debug Tools

**LLDB Scripts**: `platform/rv32/script/debug/lldb_rv32.txt`

**Memory Validation**:
```c
platform_mem_validate_critical();  // Check critical memory regions
platform_mem_print_layout();       // Display memory layout
```

**Register Inspection**:
```
(lldb) register read a0 a1 a2 a3 a4 a5 a6 a7  // Arguments
(lldb) register read t0 t1 t2 t3 t4 t5 t6     // Temporaries
(lldb) register read sp ra pc                  // Stack, return, PC
(lldb) register read sstatus sie scause sepc   // Supervisor CSRs
```

## Known Issues and Limitations

**32-bit Address Space**: Maximum 4 GiB addressable memory
- Stack and heap must fit within this limit
- 64-bit values require special handling in MMIO

**OpenSBI Dependency**: Kernel must be loaded by OpenSBI
- Cannot access M-mode resources directly
- Timer and shutdown require SBI calls

**No MMU**: Identity-mapped physical addresses
- No memory protection between kernel components
- No virtual memory management

**FDT Parsing**: Device discovery relies on correct FDT
- Fallback addresses used if FDT parsing fails
- Limited FDT parser implementation (timer frequency only)

**PLIC Complexity**: Interrupt routing requires careful configuration
- Context selection critical (S-mode = context 1)
- Priority and threshold must be configured correctly
- Claim/complete protocol required for each interrupt

## References

**Key Files**:
- `platform/rv32/linker.ld`: Memory layout and kernel base
- `platform/rv32/platform_impl.h`: Platform-specific definitions and addresses
- `platform/rv32/platform_init.c`: Initialization and device discovery
- `platform/rv32/interrupt.c`: PLIC and interrupt handling
- `platform/rv32/uart.c`: NS16550A UART driver
- `platform/rv32/timer.c`: SBI timer interface
- `platform/rv32/boot.S`: Boot assembly and stack setup

**Documentation**:
- `doc/api.md`: API architecture and layers
- `doc/virtio.md`: VirtIO implementation details
- `doc/async.md`: Async work queue design

# vmos Implementation Plan

## Project Overview

vmos is a minimal OS kernel study focused on understanding initialization sequences and basic device drivers across three architectures: x64, arm64, and rv64. The kernel runs entirely in kernel space (no MMU setup) on QEMU virtual machines with minimal hardware configuration.

## Architecture

### Language and Build System

- **Languages**: C and assembly (where needed for boot/initialization)
- **Build System**: Make with unified build across all architectures
- **Code Organization**: Platform-specific code in
  `platform/{x64,rv64,arm64}/`, no `#ifdef` in code, no conditionals in
  Makefile beyond platform selection

### Directory Structure

```
vmos/
├── Makefile                 # Unified build system
├── platform/
│   ├── arm64/
│   │   ├── boot.S          # Boot assembly
│   │   ├── devicetree.c    # Device tree parsing
│   │   ├── timer.c         # Timer driver
│   │   ├── interrupt.c     # Interrupt controller
│   │   └── linker.ld       # Linker script
│   ├── rv64/
│   │   └── [same structure]
│   └── x64/
│       └── [same structure]
├── kernel/
│   ├── main.c              # Common kernel entry point
│   ├── malloc.c            # Simple memory allocator
│   ├── string.c            # String utilities
│   └── printf.c            # Formatted output
├── driver/
│   └── virtio/
│       ├── virtio_net.c    # virtio Network device driver
│       ├── virtio_blk.c    # virtio Block device driver
│       └── virtio_rng.c    # virtio RNG driver
```

## QEMU Virtual Machine Configuration

All three platforms use QEMU with minimal virtual hardware:

### Common Configuration
- `-nodefaults` - Start with no default devices
- `-nographic` - No graphical output
- `-kernel <kernel>` - Direct kernel loading
- `-m 128M` - 128 MiB RAM
- `-smp 1` - Single CPU

### Virtual Devices (minimal set)
1. **Serial Console** - Character I/O
2. **Network Device** - Ethernet controller
3. **Block Device** - Virtual disk
4. **RNG** - Hardware random number generator
5. **Timer/Clock** - System timer
6. **Interrupt Controller** - For interrupt handling

### Platform-Specific QEMU Machines

- Devices: timer/clock, uart, interrupt controller, virtio-net, virtio-blk,
  virtio-rng
- arm64 virt, rv64 virt, x64 microvm (or q35)

## Implementation Goals and Sequence

### Goal 1: Hello World

**Objective**: Boot the kernel and print "Hello World" to the serial console.

**Tasks**:
- Write minimal boot assembly to set up stack and jump to C
- Implement platform-specific serial console output
- Write simple C entry point that prints message
- Create linker script for kernel layout
- Verify successful boot on all three platforms

**Key Concepts**: Boot process, serial I/O, memory layout

---

### Goal 2: Device Tree Parsing
**Objective**: Parse and display the device tree provided by QEMU.

**Tasks**:
- Implement FDT (Flattened Device Tree) parser
- Walk device tree structure
- Print device tree nodes and properties
- Identify key devices (console, virtio devices, memory ranges)

**Key Concepts**: Device discovery, FDT format, memory-mapped I/O addresses

**Note**: x64 may use different device discovery mechanisms (ACPI tables or direct configuration)

---

### Goal 3: Memory Allocator
**Objective**: Build a simple memory allocator for kernel heap management.

**Tasks**:
- Determine available memory from device tree/boot info
- Implement simple bump allocator or free-list allocator
- Provide `malloc()` and `free()` functions
- Test with various allocation patterns

**Key Concepts**: Memory management, heap allocation, alignment

---

### Goal 4: Timers and Interrupts
**Objective**: Set up interrupt handling and timer interrupts.

**Tasks**:
- Initialize platform-specific interrupt controller
  - ARM64: GICv2/GICv3
  - RISC-V: PLIC (Platform-Level Interrupt Controller)
  - x64: APIC or PIC
- Configure timer device
- Install interrupt handlers
- Enable interrupts
- Handle timer ticks and print periodic messages

**Key Concepts**: Interrupt vectors, interrupt controllers, timer programming, interrupt context

---

### Goal 5: Ethernet Frame Transmission
**Objective**: Send an ethernet frame out via the virtual network device.

**Tasks**:
- Initialize virtio-net device
- Understand virtio ring structure
- Construct simple ethernet frame (e.g., ARP or custom)
- Submit frame to TX queue
- Verify transmission (via QEMU network dump or logging)

**Key Concepts**: Virtio protocol, DMA, network packet structure

---

### Goal 6: Block Device I/O
**Objective**: Write a block to the virtual block device and read it back.

**Tasks**:
- Initialize virtio-blk device
- Create block request structures
- Write data to a block
- Read data back from the same block
- Verify data integrity

**Key Concepts**: Block I/O, virtio-blk protocol, synchronous I/O

---

### Goal 7: Hardware RNG Usage
**Objective**: Use the virtio-rng device to get random numbers.

**Tasks**:
- Initialize virtio-rng device
- Request random bytes
- Display random data
- Optionally seed a PRNG with hardware entropy

**Key Concepts**: Hardware entropy, virtio-rng protocol

## Technical Notes

### No MMU / Kernel Space Only
- All code runs with full hardware privileges
- No virtual memory translation
- Physical addresses used directly
- Simplified memory management

### Build System Design
- `ARCH` variable selects platform directory
- Platform-specific source files compiled from `platform/$(ARCH)/`
- Common code compiled from `common/`
- Linker script selected per-platform
- Single kernel binary output: `kernel-$(ARCH).elf`

### Toolchain Requirements
- **ARM64**: `aarch64-linux-gnu-gcc` or `aarch64-none-elf-gcc`
- **RISC-V**: `riscv64-linux-gnu-gcc` or `riscv64-unknown-elf-gcc`
- **x64**: `gcc` (native) or `x86_64-linux-gnu-gcc`
- QEMU: `qemu-system-aarch64`, `qemu-system-riscv64`, `qemu-system-x86_64`

### Cross-Platform Considerations
- Device discovery differs by platform (device tree vs ACPI vs hardcoded)
- Interrupt controllers are platform-specific
- Boot protocol varies (ARM64/RISC-V get device tree in register, x64 may use multiboot or custom)

## Expected Outcomes

By completing all goals, the project will demonstrate:
- Understanding of bare-metal boot processes across architectures
- Ability to write portable drivers with platform-specific implementations
- Knowledge of virtio device protocol
- Familiarity with interrupt handling and timers
- Basic device tree parsing
- Simple memory management

The resulting kernel will be minimal but functional, serving as an educational foundation for understanding OS fundamentals.

# VirtIO MMIO Implementation

This document describes the VirtIO MMIO transport implementation in vmos and its integration with the ARM64 platform.

## Overview

VirtIO is a standardized interface for virtual devices. vmos supports two VirtIO transport mechanisms:
- **VirtIO PCI** - Used on x64, accessing devices via PCI bus
- **VirtIO MMIO** - Used on ARM64, accessing devices via memory-mapped I/O

## VirtIO MMIO Transport

### Architecture

The VirtIO MMIO transport is implemented in `platform/arm64/virtio_mmio.c` and `platform/arm64/virtio_mmio.h`. Key components:

1. **MMIO Register Interface**: VirtIO devices expose control registers at fixed memory addresses
2. **Virtqueues**: Shared memory structures for data transfer (descriptor table, available ring, used ring)
3. **Interrupt Handling**: Devices signal completion via GIC (ARM Generic Interrupt Controller) interrupts

### Register Layout

VirtIO MMIO devices expose registers at the base address:
- `0x000`: Magic value (0x74726976 = "virt")
- `0x004`: Version (1 = legacy, 2 = modern)
- `0x008`: Device ID (4 = RNG, etc.)
- `0x030`: Queue select
- `0x034`: Queue num max
- `0x038`: Queue num
- `0x050`: Queue notify register
- `0x060`: Interrupt status
- `0x064`: Interrupt acknowledge
- `0x070`: Device status

**Version-specific registers:**

**Version 1 (Legacy):**
- `0x03c`: Queue align (page alignment, typically 4096)
- `0x040`: Queue PFN (Page Frame Number = physical_addr >> 12)

**Version 2+ (Modern):**
- `0x044`: Queue ready (0 = not ready, 1 = ready)
- `0x080`: Queue desc low (descriptor table physical address bits [31:0])
- `0x084`: Queue desc high (descriptor table physical address bits [63:32])
- `0x090`: Queue driver low (available ring physical address bits [31:0])
- `0x094`: Queue driver high (available ring physical address bits [63:32])
- `0x0a0`: Queue device low (used ring physical address bits [31:0])
- `0x0a4`: Queue device high (used ring physical address bits [63:32])

**Key Difference**: Version 1 uses a single contiguous memory region addressed by PFN with fixed layout, while version 2+ uses separate addresses for each virtqueue component.

### Initialization Sequence

1. **Device Discovery**: Scan memory regions for VirtIO magic value
2. **Device Reset**: Write 0 to status register
3. **Feature Negotiation**: Read device features, write driver features
4. **Virtqueue Setup**: Allocate and configure queue memory
5. **Driver Ready**: Set DRIVER_OK status bit
6. **Enable Interrupts**: Register and enable GIC interrupt

### Data Flow

**Submission Path** (in `platform_submit`):
1. Allocate descriptor from free list
2. Fill descriptor with buffer address and length
3. Add descriptor index to available ring
4. Write to notify register to kick device
5. Mark work as LIVE

**Completion Path** (in `kplatform_tick`):
1. IRQ handler sets `irq_pending` flag
2. `kplatform_tick` checks flag
3. Process all entries in used ring
4. Call `kplatform_complete_work` for each request
5. Free descriptors back to free list

## ARM64 Platform Integration

### Device Discovery

On ARM64, VirtIO MMIO device locations are discovered by scanning predefined memory ranges:

**QEMU virt machine layout:**
- Base address: `0x0a000000`
- 32 device slots, spaced `0x200` bytes apart
- Addresses: `0x0a000000`, `0x0a000200`, ..., `0x0a003e00`
- IRQs: 48-79 (GIC SPI interrupts 16-47)

The platform scans each slot, checking for:
1. Valid VirtIO magic value
2. Supported version (1 or 2)
3. Non-zero device ID

### GIC Interrupt Mapping

ARM GIC (Generic Interrupt Controller) interrupt numbering:
- **SGI (Software Generated Interrupts)**: 0-15
- **PPI (Private Peripheral Interrupts)**: 16-31
- **SPI (Shared Peripheral Interrupts)**: 32-255

VirtIO MMIO devices use SPIs. Device tree encodes SPIs as:
```
interrupts = <type, number, flags>
type=0: SPI, actual IRQ = 32 + number
flags: 0x1 = edge-triggered, 0x4 = level-triggered
```

Example: `interrupts = <0 16 1>` → IRQ 48 (edge-triggered)

**GIC Configuration Requirements:**
1. **GICD_ICFGR** (Interrupt Configuration Register): Bit 1 of each 2-bit field sets edge (1) vs. level (0)
2. **GICD_ITARGETSR** (Interrupt Processor Targets): 8-bit mask indicating which CPUs receive the interrupt
3. **GICD_IPRIORITYR** (Interrupt Priority): Lower values = higher priority (0xa0 is default)
4. **GICD_ISENABLER** (Interrupt Set-Enable): Bit set = interrupt enabled

**Note**: On single-CPU systems, QEMU's GICv2 implementation may hardwire ITARGETSR to CPU 0, causing reads to return 0x00 even when interrupts work correctly.

### Platform Initialization Flow

**`platform_init()` in `platform/arm64/platform_init.c`:**

1. Initialize exception vectors and GIC
2. Initialize ARM Generic Timer
3. Parse device tree (currently display only)
4. Scan VirtIO MMIO slots:
   - For each slot 0-31:
     - Calculate address: `0x0a000000 + (i * 0x200)`
     - Calculate IRQ: `48 + i`
     - Call `virtio_rng_mmio_setup()`
     - Break if device found

**`virtio_rng_mmio_setup()` in `platform/arm64/virtio_mmio.c`:**

1. Verify VirtIO magic value
2. Check version compatibility (1 or 2)
3. Check device ID (4 = RNG)
4. Initialize device (reset, feature negotiation)
5. Setup virtqueue in static memory
6. Configure queue addresses in device registers
7. Register IRQ handler with GIC
8. Enable interrupts
9. Set DRIVER_OK status

### Memory Layout

```
Static allocation in virtio_mmio.c:
- g_virtqueue_memory[64KB]: Virtqueue data structures
  - Descriptor table: 256 entries × 16 bytes
  - Available ring: 256 entries × 2 bytes + header
  - Used ring: 256 entries × 8 bytes + header
- g_virtio_rng: Device state structure
```

## Current Status

### Working Features

✅ x64 VirtIO-RNG via PCI transport (fully working)
✅ ARM64 VirtIO MMIO device discovery
✅ ARM64 VirtIO-RNG initialization via MMIO
✅ ARM64 GIC interrupt configuration (edge/level triggering)
✅ ARM64 interrupt handler registration
✅ ARM64 work submission (descriptors added to queue)
✅ ARM64 device notification (QUEUE_NOTIFY register)
✅ VirtIO MMIO v1 vs v2 detection and handling

### Not yet working

- Verifying that the device is receiving and processing requests
- Device generating interrupts
- Servicing the interrupt
- Handling in the main loop

## Code Organization

```
platform/arm64/
  virtio_mmio.h          - VirtIO MMIO structures and register definitions
                         - Includes v1 (QUEUE_PFN) and v2 (separate addresses) registers
  virtio_mmio.c          - VirtIO MMIO transport implementation
                         - Contains kplatform_tick() and platform_submit()
                         - Version detection and queue setup (v1 vs v2)
  interrupt.c            - GIC initialization and interrupt handling
                         - irq_register() with edge/level trigger configuration
                         - irq_dump_config() for debugging interrupt state
  interrupt.h            - Interrupt management interface
  platform_init.c        - Platform initialization, device scanning

platform/x64/
  virtio_pci.h           - VirtIO PCI structures (PCI-specific)
  virtio_pci.c           - VirtIO PCI transport implementation
  pci.c, pci_scan.c      - PCI bus scanning and configuration

src/virtio/
  virtio.h               - Platform-agnostic VirtIO structures
  virtio.c               - Virtqueue management (descriptor allocation, etc.)

src/
  devicetree.c           - Device tree parsing (ARM64)
  fdt.h                  - FDT structure definitions
```

## Debugging Tools

The following debugging tools have been added to diagnose interrupt and device issues:

### GIC Configuration Dump
```c
void irq_dump_config(uint32_t irq_num);
```
Located in `platform/arm64/interrupt.c:318-374`. Prints:
- Enabled state
- Priority level
- Target CPU mask
- Trigger type (edge/level)
- GIC distributor and CPU interface state

Called automatically after IRQ registration in VirtIO MMIO setup.

### VirtIO Queue Status Polling
Temporary debug code in `kplatform_tick()` (`platform/arm64/virtio_mmio.c:248-283`):
- Polls INTERRUPT_STATUS register every tick
- Checks used ring index for updates
- Prints debug info every 10 ticks
- **TODO**: Remove once device communication is fixed

### Verbose Queue Notification
Debug logging in `platform_submit()` (`platform/arm64/virtio_mmio.c:382-392`):
- Logs number of requests submitted
- Shows available ring index
- Confirms QUEUE_NOTIFY register write

## Testing

### x64 (PCI Transport)
```bash
make run ARCH=x64
```

**Expected output:**
```
Found VirtIO-RNG at PCI 0:1.0
VirtIO-RNG initialized successfully
RNG request submitted
Random bytes (32): 0xfb 0x2c 0xc0 ... [success]
```

### ARM64 (MMIO Transport)
```bash
make run ARCH=arm64
```

**Current output (as of 2025-10-27):**
```
Scanning for VirtIO-RNG...
VirtIO MMIO device at 0x0x000000000a000000, IRQ 48
VirtIO MMIO version: 1
Reading device ID...
Device ID: 0
No device at this address
...
VirtIO MMIO device at 0x0x000000000a003e00, IRQ 79
VirtIO MMIO version: 1
Reading device ID...
Device ID: 4
Found VirtIO-RNG (MMIO) device
Resetting device...
Acknowledging device...
Setting driver status...
Reading features...
Device features: 0x0x39000000
Setting driver features...
Setting FEATURES_OK...
Verifying FEATURES_OK...
Status after FEATURES_OK: 0x0x0b
Queue size: 256
Using legacy QUEUE_PFN setup (v1)
Queue PFN: 0x0x00040216 (addr: 0x0x0000000040216000)
Cleaning virtqueue cache...
IRQ 79 registered (edge-triggered, target CPU 0)
IRQ 79 enabled in GIC
GIC configuration for IRQ 79:
  Enabled: yes
  Priority: 0x0xa0
  Target CPU mask: 0x0x00
  Trigger: edge
  GICC_CTLR: 0x0x00000001, GICC_PMR: 0x0x000000ff
  GICD_CTLR: 0x0x00000001
VirtIO-RNG (MMIO) initialized successfully
VirtIO-RNG found at slot 31

Platform initialization complete.

kinit complete
[KMAIN] kinit ok
kusermain: Requesting 32 random bytes...
RNG request submitted
[KMAIN] kusermain ok
[KMAIN] kloop...
[KLOOP] tick
[KLOOP] wfi
Timer set for 2000ms (125000000 ticks)
[KLOOP] tick
[KLOOP] wfi
Timer set for 2000ms (125000000 ticks)
[KLOOP] tick
[KLOOP] wfi
Timer set for 2000ms (125000000 ticks)

```

**Note**: Device initialization and notification work, but device doesn't process requests.

## Future Work

1. **Fix ARM64 cache coherency** (blocking RNG functionality) - Add cache maintenance operations or switch to VirtIO MMIO v2
2. **Complete device tree parsing** for proper dynamic device discovery
3. **Remove debug polling code** from kplatform_tick once interrupts work
4. **Clean up GIC target CPU register handling** - investigate why ITARGETSR reads as 0x00
5. **Add support for other VirtIO devices** (block, network)
6. **Implement MSI-X** for better interrupt performance (x64)
7. **Add VirtIO MMIO v2 optimizations** (packed queues, indirect descriptors)

## References

- [VirtIO Specification 1.1](https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.html)
- [ARM GIC Architecture Specification](https://developer.arm.com/documentation/ihi0069/latest/)
- [Device Tree Specification](https://devicetree-specification.readthedocs.io/)
- [QEMU virt machine documentation](https://www.qemu.org/docs/master/system/arm/virt.html)

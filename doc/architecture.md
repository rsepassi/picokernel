# VMOS Architecture

## Overview

VMOS is a minimal OS kernel study project targeting virtual machines. It demonstrates kernel initialization sequences and basic device driver development across multiple architectures (x64, rv32, rv64, arm32, arm64). The kernel runs entirely in kernel space without MMU setup and interacts with VirtIO virtual devices using an asynchronous work queue pattern.

**Key Characteristics:**
- No processes, no virtual memory, no filesystem
- Single-threaded event loop with async I/O
- Hardware abstraction through platform layer
- VirtIO device support (RNG, block, network)
- Multi-architecture support (5 platforms)

## System Architecture

VMOS uses a strict **3-layer architecture** with unidirectional dependencies:

```
┌─────────────────────────────────────────┐
│  Layer 3: Application (User)            │  app/user.c, user.h
│  - User code and application logic      │
│  - Submits async work via kapi.h        │
└─────────────────┬───────────────────────┘
                  │ ksubmit(), kcancel()
                  ▼
┌─────────────────────────────────────────┐
│  Layer 2: Kernel / Runtime               │  kernel/kmain.c, kernel.c
│  - Event loop and work queue management  │  kernel/kernel.h, kapi.h
│  - Timer heap and callback dispatch      │
│  - Platform-agnostic coordination        │
└─────────────────┬───────────────────────┘
                  │ platform_submit(), platform_wfi()
                  ▼
┌─────────────────────────────────────────┐
│  Layer 1: Platform Abstraction           │  platform/*/platform_init.c
│  - Hardware-specific initialization      │  platform/*/platform_virtio.c
│  - Interrupt handling and device enum    │  platform.h, platform_impl.h
│  - PCI/MMIO device discovery             │
└─────────────────┬───────────────────────┘
                  │ Direct device operations
                  ▼
┌─────────────────────────────────────────┐
│  Layer 0: Device Drivers                 │  driver/virtio/*.c
│  - VirtIO core (virtqueue management)    │  driver/virtio/*.h
│  - VirtIO transports (MMIO, PCI)         │
│  - Device drivers (RNG, block, network)  │
└─────────────────────────────────────────┘
```

**Dependency Rule:** Dependencies flow downward only. Upper layers depend on lower layers, never the reverse. The kernel calls platform functions; the platform calls back via `kplatform_complete_work()`.

## Header Layering

Headers enforce the architectural layers through explicit dependencies:

```
user.h (Application API)
  ↓ includes
kapi.h (Kernel API - async work submission)
  ↓ includes
platform.h (Platform contract - what implementations must provide)
  ↓ includes
kbase.h (Foundation - container macros, alignment, memory ops)
  ↓ includes
<stdint.h>, <stddef.h>, etc. (Standard C headers)
```

Additional internal headers:
- **kernel.h**: Internal kernel structures and lifecycle functions (`kmain_init`, `kmain_tick`, `kplatform_complete_work`)
- **platform_impl.h**: Platform-specific types (one per platform directory, defines `platform_t`)

**Key Headers:**

- **kbase.h** - Foundation utilities with zero dependencies (except libc)
  - Container macros (`CONTAINER_OF`, `ARRAY_SIZE`)
  - Alignment (`KALIGN`, `IS_ALIGNED`)
  - Min/max, memory operations
  - Type definitions (`ktime_t`, `kregions_t`)

- **platform.h** - Complete platform contract (9 sections)
  1. Memory management
  2. PCI configuration space
  3. MMIO register access
  4. Platform lifecycle (`platform_init`, `platform_wfi`, `platform_submit`)
  5. Debug output (UART)
  6. Interrupt control (enable/disable)
  7. IRQ management
  8. Device discovery

- **platform_impl.h** - Platform-specific implementation
  - Defines `platform_t` structure (one definition per platform)
  - Embeds device state (interrupt controller, timer, VirtIO devices)
  - Platform-specific request fields (e.g., `krng_req_platform_t`)

- **kapi.h** - User-facing kernel API
  - Work item structures (`kwork_t`, `ktimer_req_t`, `krng_req_t`, `kblk_req_t`, `knet_recv_req_t`, `knet_send_req_t`)
  - Work states (`DEAD`, `SUBMIT_REQUESTED`, `LIVE`, `READY`)
  - Operation types (`KWORK_OP_TIMER`, `KWORK_OP_RNG_READ`, etc.)
  - Submission functions (`ksubmit()`, `kcancel()`)
  - Error codes (`KERR_OK`, `KERR_BUSY`, etc.)

- **kernel.h** - Internal kernel implementation
  - `kernel_t` structure (work queues, timer heap, platform state)
  - Lifecycle functions (`kmain_init`, `kmain_tick`, `kmain_next_delay`)
  - Platform→Kernel callbacks (`kplatform_complete_work`, `kplatform_cancel_work`)

- **user.h** - Application entry point
  - `user_main()` function signature
  - User application initialization

## Event Loop Pattern

VMOS uses a **single-threaded event loop** with async I/O, similar to Node.js or embedded RTOS patterns:

```c
void kmain(void *fdt) {
    // 1. Initialize kernel and platform
    kernel_t *k = &g_kernel;
    kmain_init(k, fdt);                    // kernel/kmain.c:28

    // 2. Kick off user application
    user_t *user = &g_user;
    user->kernel = k;
    user_main(user);                       // app/user.c

    // 3. Event loop
    while (1) {
        kmain_tick(k, k->current_time_ns); // Process work
        ktime_t timeout = kmain_next_delay(k);
        k->current_time_ns = platform_wfi(&k->platform, timeout);
    }
}
```

### Event Loop Steps

Each iteration of the event loop consists of:

1. **`kmain_tick(k, current_time)`** (kernel/kernel.c:200+):
   - Update current time
   - Expire timers (heap traversal)
   - Process ready queue (invoke callbacks)
   - Submit queued work to platform

2. **`kmain_next_delay(k)`** (kernel/kernel.c:150+):
   - Calculate next timer deadline
   - Return timeout duration for WFI

3. **`platform_wfi(platform, timeout)`** (platform/*/platform_init.c):
   - Wait for interrupt with timeout
   - Process interrupts when they arrive
   - Return updated current time

### Interrupt Handling Flow

Interrupts use a **deferred processing pattern**:

1. **IRQ Handler** (minimal work in IRQ context):
   - Set flag or add to IRQ ring buffer
   - DO NOT call `printk()` (not reentrant)
   - DO NOT do heavy processing

2. **`kplatform_tick()`** (called from `kmain_tick` before callbacks):
   - Process deferred IRQ work
   - Move completed work from `LIVE` to `READY` state
   - Call `kplatform_complete_work(k, work, result)`

3. **Callback Execution**:
   - Kernel invokes user callback
   - Work transitions to `DEAD` (or stays `LIVE` if `KWORK_FLAG_STANDING`)
   - User may resubmit work if needed

**Critical Rule:** Interrupts are enabled AFTER platform initialization completes (`platform_interrupt_enable()`) to avoid spurious interrupts during device enumeration.

## Work Flow Through the System

### Async Work Submission Lifecycle

```
User Application (app/user.c)
    │
    │ 1. Create work item (krng_req_t, ktimer_req_t, etc.)
    │    Set callback, context, operation type
    │
    ▼
ksubmit(kernel, &work->work)  (kapi.h)
    │
    │ 2. Validate state (must be DEAD)
    │    Mark as SUBMIT_REQUESTED
    │    Add to kernel submit_queue
    │
    ▼
kmain_tick() processes submit_queue  (kernel.c)
    │
    │ 3. Move from submit_queue to platform
    │    Mark work as LIVE
    │
    ▼
platform_submit(platform, submissions, cancellations)  (platform/*/platform_virtio.c)
    │
    │ 4a. Timer: Insert into platform timer subsystem
    │ 4b. Device I/O: Submit to VirtIO driver
    │
    ▼
[Async processing happens...]
    - Interrupt arrives OR timer expires
    - IRQ handler sets flag
    - kplatform_tick() processes deferred work
    │
    ▼
kplatform_complete_work(kernel, work, result)  (kernel.c)
    │
    │ 5. Move from LIVE to READY
    │    Set result code
    │    Add to ready_queue
    │
    ▼
kmain_tick() processes ready_queue
    │
    │ 6. Invoke callback: work->callback(work)
    │    Mark work as DEAD (or keep LIVE if STANDING)
    │
    ▼
User callback runs (app/user.c)
    │
    │ 7. User processes result
    │    May resubmit work if needed
```

### Work State Transitions

```
DEAD (0)
  │
  │ ksubmit()
  ▼
SUBMIT_REQUESTED (1)
  │
  │ kmain_tick() bulk submission
  ▼
LIVE (2)
  │
  │ Interrupt/completion
  │ kplatform_complete_work()
  ▼
READY (3)
  │
  │ kmain_tick() callback
  ▼
DEAD (0) [or back to LIVE if KWORK_FLAG_STANDING]
```

**Special States:**
- **CANCEL_REQUESTED (4)**: User called `kcancel()`, best-effort cancellation attempted
- **STANDING work**: Remains in `LIVE` state after callback, doesn't transition to `DEAD`

## Directory Structure

```
vmos/
├── app/              # User application code
│   ├── user.c        # User entry point (user_main)
│   └── user.h        # User API
│
├── kernel/           # Core kernel implementation
│   ├── kmain.c       # Kernel entry and event loop
│   ├── kernel.c      # Work queue and timer heap
│   ├── kernel.h      # Internal kernel API
│   ├── kapi.h        # User-facing kernel API
│   ├── kbase.h       # Foundation utilities
│   ├── platform.h    # Platform contract
│   ├── printk.c      # Debug printing
│   └── csprng.c      # Cryptographic PRNG
│
├── platform/         # Platform-specific implementations
│   ├── arm32/        # 32-bit ARM
│   ├── arm64/        # 64-bit ARM (AArch64)
│   ├── rv32/         # 32-bit RISC-V
│   ├── rv64/         # 64-bit RISC-V
│   ├── x64/          # x86-64
│   └── shared/       # Shared platform utilities
│       └── fdt.c     # Device tree parsing
│
├── driver/           # Device drivers
│   └── virtio/       # VirtIO subsystem
│       ├── virtio.h              # Core virtqueue structures
│       ├── virtio_mmio.h/c       # MMIO transport
│       ├── virtio_pci.h/c        # PCI transport
│       ├── virtio_rng.h/c        # RNG device
│       ├── virtio_blk.h/c        # Block device
│       └── virtio_net.h/c        # Network device
│
├── libc/             # Minimal libc implementation
│   └── string.c      # memcpy, memset, strlen, etc.
│
├── vendor/           # Third-party code
│   └── libfdt/       # Device tree library
│
├── doc/              # Documentation
│   ├── architecture.md      # This file
│   ├── async-work.md        # Async work queue details
│   ├── platform-api.md      # Platform contract reference
│   ├── virtio.md            # VirtIO implementation
│   └── design/              # Future design documents
│
├── build/            # Build output (per-platform)
│   └── <platform>/   # Generated binaries and objects
│
└── script/           # Build and test scripts
```

### Per-Platform Structure

Each platform directory (`platform/arm64/`, `platform/x64/`, etc.) contains:

```
platform/arm64/
├── platform_impl.h      # Platform-specific types (platform_t definition)
├── platform.mk          # Build configuration (QEMU, compiler flags)
├── boot.S               # Assembly boot code
├── platform_init.c      # Initialization and WFI
├── platform_virtio.c    # Device discovery and work submission
├── interrupt.c          # Interrupt controller (GIC, APIC, PLIC, etc.)
├── timer.c              # Platform timer
└── mmio.c               # MMIO register access with barriers
```

## Key Files

### Kernel Core

- **kernel/kmain.c** - Entry point (`kmain()`), event loop
- **kernel/kernel.c** - Work queue management, timer heap, tick processing
- **kernel/kapi.h** - Public API for submitting async work
- **kernel/kernel.h** - Internal kernel structures
- **kernel/kbase.h** - Foundation macros and types
- **kernel/platform.h** - Complete platform contract

### Platform Implementations

- **platform/*/boot.S** - Assembly entry point, stack setup, jump to `kmain()`
- **platform/*/platform_init.c** - `platform_init()`, `platform_wfi()`, UART initialization
- **platform/*/platform_virtio.c** - Device discovery (PCI/MMIO), `platform_submit()`
- **platform/*/interrupt.c** - Interrupt controller setup and IRQ handling
- **platform/*/timer.c** - Platform timer configuration
- **platform/*/platform_impl.h** - Platform-specific `platform_t` definition

### VirtIO Drivers

- **driver/virtio/virtio.h** - Core virtqueue structures (rings, descriptors)
- **driver/virtio/virtio_mmio.c** - MMIO transport implementation
- **driver/virtio/virtio_pci.c** - PCI transport implementation
- **driver/virtio/virtio_rng.c** - RNG device driver
- **driver/virtio/virtio_blk.c** - Block device driver
- **driver/virtio/virtio_net.c** - Network device driver

### Build System

- **Makefile** - Top-level build orchestration
- **platform/*/platform.mk** - Platform-specific build configuration
- **linker/*.ld** - Linker scripts per platform

## Device Discovery

VMOS supports two device transport methods:

### PCI Transport (USE_PCI=1)

1. Scan PCI configuration space:
   - x64: I/O port access (0xCF8/0xCFC)
   - ARM64/RISC-V: ECAM (Enhanced Configuration Access Mechanism)

2. Enumerate bus/slot/function triplets

3. Identify VirtIO devices (Vendor ID 0x1AF4)

4. Read BARs to get MMIO regions

5. Initialize VirtIO PCI transport

**Files:** `platform/*/platform_virtio.c`, `driver/virtio/virtio_pci.c`

### MMIO Transport (USE_PCI=0, default)

1. Probe known MMIO addresses:
   - From device tree (FDT on ARM/RISC-V)
   - Hardcoded ranges (x86 without device tree)

2. Read VirtIO magic number (0x74726976)

3. Identify device type from device ID register

4. Initialize VirtIO MMIO transport

**Files:** `platform/*/platform_virtio.c`, `driver/virtio/virtio_mmio.c`

## Build Configuration

Each platform has a `platform.mk` defining:

```make
TARGET = aarch64-none-elf              # LLVM target triple
QEMU = qemu-system-aarch64             # QEMU binary
QEMU_MACHINE = virt                    # QEMU machine type
QEMU_CPU = cortex-a72                  # CPU model

PLATFORM_C_SRCS = \                    # Platform C sources
    platform/arm64/platform_init.c \
    platform/arm64/platform_virtio.c \
    ...

PLATFORM_S_SRCS = \                    # Platform assembly sources
    platform/arm64/boot.S

PLATFORM_SHARED_SRCS = \               # Shared kernel sources
    kernel/kmain.c \
    kernel/kernel.c \
    ...

PLATFORM_CFLAGS = -mcpu=cortex-a72     # Compiler flags
PLATFORM_LDFLAGS = -T linker/arm64.ld  # Linker flags
```

**Build commands:**
```bash
make PLATFORM=arm64                    # Build for arm64
make run PLATFORM=arm64                # Build and run in QEMU
make test PLATFORM=arm64 USE_PCI=1     # Test with PCI transport
make clean                             # Clean all platforms
make format                            # Format all source code
```

## Naming Conventions

Function prefixes indicate scope and layer:

- **`k*`** - Kernel utilities (kbase.h): `KALIGN()`, `KMIN()`, `KMAX()`
- **`kmain_*`** - Main kernel lifecycle (kernel.h): `kmain_init()`, `kmain_tick()`
- **`kplatform_*`** - Platform→Kernel calls (kernel.h): `kplatform_complete_work()`
- **`platform_*`** - Kernel→Platform calls (platform.h): `platform_init()`, `platform_wfi()`
- **`krng_*`, `ktimer_*`, `kblk_*`, `knet_*`** - Device-specific work types
- **`virtio_*`** - VirtIO driver functions

Type suffix:
- **`_t`** - Typedef'd structures: `kernel_t`, `platform_t`, `kwork_t`

## Memory Model

VMOS runs in **kernel space only** with no MMU configuration:

- **No virtual memory**: Physical addresses used directly
- **No processes**: Single address space, no isolation
- **Stack**: Set up in `boot.S`, typically 16KB-64KB per platform
- **Heap**: Not implemented (all allocations are static or on stack)
- **Device memory**: MMIO regions accessed via platform functions

**Memory regions** are discovered via device tree or platform-specific methods and returned by `platform_mem_regions()`.

## Concurrency Model

- **Single-threaded**: No SMP support, one CPU core only
- **Async I/O**: Work submission returns immediately, completion via callback
- **No blocking**: Everything is async or polled
- **Cooperative**: No preemption, work runs to completion
- **Interrupt-driven**: Devices signal completion via interrupts

## Design Principles

1. **Layered Dependencies**: Strict unidirectional dependencies (up → down)
2. **Platform Abstraction**: All hardware access through platform.h contract
3. **Async-First**: All I/O is asynchronous with callbacks
4. **Zero Allocation**: No dynamic memory allocation (stack and static only)
5. **Minimal Kernel**: Only essential features, no unnecessary complexity
6. **Multi-Architecture**: Same kernel code runs on 5 different platforms
7. **Educational**: Clear code structure for learning kernel development

## What VMOS Does NOT Have

To keep the scope minimal, VMOS intentionally omits:

- ❌ Memory management (no heap allocator, no MMU)
- ❌ Process management (no processes, no threads)
- ❌ Scheduling (single event loop, no scheduler)
- ❌ Filesystem (no VFS, no file operations)
- ❌ System calls (no user/kernel separation)
- ❌ Networking stack (raw packets only, no TCP/IP)
- ❌ SMP support (single CPU core)
- ❌ Security features (no isolation, no permissions)

## Next Steps

For detailed information on specific subsystems, see:

- **[async-work.md](async-work.md)** - Work queue system, states, submission, timers
- **[platform-api.md](platform-api.md)** - Platform contract reference and porting guide
- **[virtio.md](virtio.md)** - VirtIO subsystem architecture and drivers

For future design ideas (not current implementation):
- **[design/multimachine.md](design/multimachine.md)** - Machine pattern architecture vision

# VMOS - a portable picokernel for async I/O

VMOS is an experimental minimal kernel that serves as a portable async runtime
on bare metal. It's so tiny, so under-featured, so unsafe that it barely
qualifies as a kernel - but that's the point.

## Philosophy

Hardware doesn't manage itself via threads. It operates through state and
events: submit work, go idle, respond to interrupts. VMOS aligns with this
model rather than imposing traditional kernel abstractions. Everything is
cooperative, non-blocking, and callback-driven. It's more like a piece of
embedded system firmware than an operating system.

## What It Has

- Debug logging
- Memory discovery and allocation
- Time and timers
- Event loop with async work queue
- Multi-architecture support
- Hardware RNG (virtio-rng)
- Block storage (virtio-blk)
- UDP/IP networking with ARP and ICMP (virtio-net)

## What It Doesn't Have

- Memory protection or privilege levels
- Filesystems
- Preemptive scheduling or context switches
- TCP stack
- Security model of any kind

## Platforms

VMOS runs on QEMU virtual machines across five architectures:

- **arm64** - ARM 64-bit
- **arm32** - ARM 32-bit
- **x64** - Intel/AMD 64-bit
- **rv64** - RISC-V 64-bit
- **rv32** - RISC-V 32-bit

All platforms use VirtIO devices (MMIO or PCI transport).

## Quick Start

Build and run for a specific platform:
```bash
make run PLATFORM=arm64
```

Test a configuration:
```bash
make test PLATFORM=x64 USE_PCI=1
```

## Implementation

Approximately 12,000 lines of freestanding C11 code with no dependencies
outside the C11 freestanding headers and build/run tooling (`make`, `sh`,
`clang`, `qemu-system-*`).

| Directory          | C       | Headers | Assembly | Total   |
|--------------------|---------|---------|----------|---------|
| kernel/            |   1,503 |     536 |        0 |   2,039 |
| driver/            |   1,151 |     414 |        0 |   1,565 |
| platform/shared/   |   1,004 |      31 |        0 |   1,035 |
| platform/arm64/    |   1,420 |     127 |      268 |   1,815 |
| platform/arm32/    |     915 |     122 |      105 |   1,142 |
| platform/x64/      |   2,950 |     406 |      330 |   3,686 |
| platform/rv64/     |   1,319 |     150 |      110 |   1,579 |
| platform/rv32/     |   1,065 |     146 |       85 |   1,296 |

The core is a simple event loop that processes completions, expires timers,
calculates the next timeout, and waits for interrupts. User code submits async
work requests (timers, RNG, block I/O, network packets) by configuring work
items with callbacks. The platform processes work asynchronously and invokes
callbacks upon completion.

## Documentation

See `doc/` for detailed architecture documentation:
- `doc/api.md` - Layered API architecture
- `doc/async.md` - Async work queue design
- `doc/virtio.md` - VirtIO implementation details
- `doc/memory.md` - Memory management

## Status

Active development. Runs successfully across all five platforms with working
device drivers and networking stack. Future directions include microcontroller
support, hosted implementations, and multi-core capability.

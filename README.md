# vmos - Virtual Machine OS

A minimal OS kernel study project targeting virtual machines. The goal is to
understand kernel initialization sequences and basic device driver development
across multiple architectures.

The project supports three architectures using QEMU:
- **x64** - x86-64 (amd64)
- **arm64** - ARMv8 64-bit
- **rv64** - RISC-V 64-bit

All implementations run in kernel space (no MMU setup) and interact with a
minimal set of virtual devices.

## Goals

1. Boot and print "Hello World" to console
1. Parse and display the device tree
1. Implement a simple memory allocator
1. Set up timers and interrupt handling
1. Use the hardware RNG
1. Send and receive ethernet frames
1. Write and read blocks from block device

## Virtual Hardware

Each QEMU machine is configured with minimal devices:
- 1 CPU
- 128 MiB RAM
- Timer/clock
- Serial console
- RNG device
- Network device
- Block device

## Building and Running

Build for a specific architecture:
```bash
make ARCH=rv64
make ARCH=arm64
make ARCH=x64
```

Build and run:
```bash
make run ARCH=rv64
make run ARCH=arm64
make run ARCH=x64
```

## Project Structure

```
vmos/
├── platform/
│   ├── arm64/    # ARM64-specific code
│   ├── rv64/     # RISC-V 64-bit code
│   └── x64/      # x86-64 code
├── src/          # Shared kernel code
└── doc/          # Documentation
```

# vmos - Virtual Machine OS

A minimal OS kernel study project targeting virtual machines. The goal is to
understand kernel initialization sequences and basic device driver development
across multiple architectures.

The project supports the following architectures using QEMU:

```
x64
x32
rv32
rv64
arm32
arm64
```

All implementations run in kernel space (no MMU setup) and interact with a
minimal set of virtual devices.

## Goals

1. Boot and print "Hello World" to console
1. Parse and display the device tree
1. Set up timers and interrupt handling
1. Use the hardware RNG
1. Send and receive ethernet frames
1. Write and read blocks from block device
1. Implement a simple memory allocator

## Virtual Hardware

Each QEMU machine is configured with minimal devices:
- 1 CPU
- 128 MiB RAM
- Timer/clock
- RNG device
- Serial console
- Network device
- Block device

## Building and Running

Build for a specific architecture:
```bash
make ARCH=x64
```

Build and run:
```bash
make run ARCH=x64
```

## Project Structure

```
vmos/
├── platform/     # platform-specific code
│   ├── x64/      # x64-specific code
│   └── .../
├── src/          # Shared kernel code
└── doc/          # Documentation
```

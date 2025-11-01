# ARM64 Platform Memory Map

## Physical Memory Layout

### RAM Region (0x40000000+)

```
0x40000000 - 0x401FFFFF:  Device Tree Blob (DTB) region (2 MiB reserved)
                          - QEMU places DTB at 0x40000000 for ELF kernels
                          - Kernel offset to avoid collision

0x40200000:               Kernel base
                          - .text.boot (entry point)
                          - .text section
                          - .rodata section
                          - .data section
                          - .bss section:
                            - g_kernel (platform_t)
                            - g_user
                            - stack (64 KiB)

0x40300000:               Stack top (approximate)
                          - Stack grows downward from here

End of kernel (_end):     Varies by build (~240 KiB kernel size)
```

### Memory-Mapped I/O

```
0x08000000 - 0x08000FFF:  VirtIO MMIO base (device 0)
                          - Devices at 0x200 byte intervals:
                          - 0x08000000, 0x08000200, 0x08000400...

0x09000000 - 0x09001FFF:  PL011 UART (8 KiB region)
                          - Debug output via platform_putc()

0x08020000:               PCI ECAM base (if USE_PCI=1)
                          - Extended Configuration Access Mechanism
```

## Key Details

**QEMU Machine**: `virt` (ARM64)

**Boot Sequence**:
1. `boot.S` entry at `_start`
2. Set stack pointer to `stack_top` (~0x40300000)
3. Clear `.bss` section
4. Load DTB pointer (0x40000000) into x0
5. Branch to `kmain`

**DTB Collision Avoidance**: Kernel loaded at 2 MiB offset (0x40200000) to leave space for DTB at start of RAM

**Exception Levels**: Kernel runs in EL1 (supervisor mode)

**Memory Model**: No MMU setup, identity-mapped physical addresses

## Debug Tools

**LLDB scripts**: `platform/arm64/script/debug/lldb_arm64.txt`

**Memory validation**:
```c
platform_mem_validate_critical();  // Check kernel sections, DTB region
platform_mem_print_layout();       // Dump ARM64 memory map
```

**Register inspection**:
```
(lldb) register read x0 x1 x2 x3 x4 x5 x6 x7
(lldb) register read x29 x30 sp pc
(lldb) register read CurrentEL  // Check exception level
```

## Known Issues

**DTB Location**: Must parse DTB at 0x40000000 early in boot to discover:
- VirtIO MMIO device addresses
- UART base address
- Interrupt controller base

**Stack Size**: 64 KiB stack may be insufficient for deep call chains with large locals

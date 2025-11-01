# ARM32 Platform Memory Map

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

End of kernel (_end):     Varies by build
```

### Memory-Mapped I/O

```
0x08000000 - 0x08000FFF:  VirtIO MMIO base (device 0)
                          - Devices at 0x200 byte intervals

0x09000000 - 0x09001FFF:  PL011 UART (8 KiB region)
                          - Debug output via platform_putc()

0x08020000:               PCI ECAM base (if USE_PCI=1)
```

## Key Details

**QEMU Machine**: `virt` (ARM Cortex-A15)

**Boot Sequence**:
1. `boot.S` entry at `_start`
2. Set stack pointer to `stack_top` (~0x40300000)
3. Clear `.bss` section
4. Load DTB pointer (0x40000000) into r0
5. Branch to `kmain`

**Addressing**: 32-bit physical addresses (max 4 GiB)

**DTB Collision Avoidance**: Kernel at 2 MiB offset (0x40200000)

**Memory Model**: No MMU setup, identity-mapped

## Debug Tools

**LLDB scripts**: `platform/arm32/script/debug/lldb_arm32.txt`

**Memory validation**:
```c
platform_mem_validate_critical();
platform_mem_print_layout();
```

**Register inspection**:
```
(lldb) register read r0 r1 r2 r3 r4 r5 r6 r7
(lldb) register read r11 r12 sp lr pc
(lldb) register read cpsr  // Check processor status
```

## Known Issues

**DTB Parsing**: Required to discover device addresses (VirtIO, UART, interrupt controller)

**32-bit Limitations**: Max 4 GiB addressable space, limits RAM size

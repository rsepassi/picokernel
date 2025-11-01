# RISC-V 32-bit Platform Memory Map

## Physical Memory Layout

### RAM Region (0x80000000+)

```
0x80000000 - 0x801FFFFF:  OpenSBI firmware region (2 MiB reserved)
                          - Supervisor Binary Interface (SBI)
                          - Machine-mode runtime
                          - Kernel offset to avoid collision

0x80200000:               Kernel base
                          - .text.boot (entry point)
                          - .text section
                          - .rodata section
                          - .data section
                          - .bss section:
                            - g_kernel (platform_t)
                            - g_user
                            - stack (64 KiB)

End of kernel (_end):     Varies by build
```

### Memory-Mapped I/O

```
0x10000000 - 0x10000FFF:  VirtIO MMIO base (device 0)
                          - Devices at 0x1000 byte intervals

0x10000000:               PL011 UART (if present)
                          - Debug output via platform_putc()

0x30000000:               PCI ECAM base (if USE_PCI=1)

0x0C000000:               PLIC (Platform-Level Interrupt Controller)
```

## Key Details

**QEMU Machine**: `virt` (RISC-V 32-bit)

**Boot Sequence**: Same as RV64
1. OpenSBI in M-mode
2. Jump to kernel at 0x80200000 in S-mode
3. `boot.S` initializes stack and BSS
4. Branch to `kmain`

**Addressing**: 32-bit physical addresses (max 4 GiB)

**Privilege Modes**:
- M-mode: OpenSBI
- S-mode: Kernel
- U-mode: Not used

**SBI Interface**: Same as RV64 (timer, console, shutdown)

**Memory Model**: Sv32 (2-level page tables), but MMU not used in VMOS

## Debug Tools

**LLDB scripts**: `platform/rv32/script/debug/lldb_rv32.txt`

**Memory validation**:
```c
platform_mem_validate_critical();
platform_mem_print_layout();
```

**Register inspection**:
```
(lldb) register read a0 a1 a2 a3 a4 a5 a6 a7  // Arguments
(lldb) register read t0 t1 t2 t3 t4 t5 t6     // Temporaries
(lldb) register read sp ra pc                  // Stack, return, PC
(lldb) register read sstatus                   // Supervisor status
```

**CSR Registers**: Same as RV64 (sstatus, sie, sip, stvec, sepc, scause, etc.)

## Known Issues

**32-bit Limitations**: Max 4 GiB addressable space

**OpenSBI Dependency**: Must be loaded by OpenSBI

**PLIC Configuration**: Complex interrupt routing

**DTB Parsing**: Required to discover device addresses

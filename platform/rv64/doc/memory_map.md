# RISC-V 64-bit Platform Memory Map

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
                          - Devices at 0x1000 byte intervals:
                          - 0x10001000, 0x10002000, 0x10003000...

0x10000000:               PL011 UART (if present)
                          - Debug output via platform_putc()

0x30000000:               PCI ECAM base (if USE_PCI=1)
                          - Extended Configuration Access Mechanism

0x0C000000:               PLIC (Platform-Level Interrupt Controller)
```

## Key Details

**QEMU Machine**: `virt` (RISC-V 64-bit)

**Boot Sequence**:
1. OpenSBI runs in M-mode, initializes hardware
2. OpenSBI jumps to kernel at 0x80200000 in S-mode
3. `boot.S` entry at `_start`
4. Set stack pointer
5. Clear `.bss` section
6. Branch to `kmain`

**Privilege Modes**:
- M-mode (Machine): OpenSBI firmware
- S-mode (Supervisor): Kernel runs here
- U-mode (User): Not used

**SBI Interface**: Kernel makes SBI calls for:
- Timer setup (`sbi_set_timer`)
- UART console I/O (`sbi_console_putchar`)
- System reset/shutdown

**Memory Model**: Sv39 (3-level page tables), but MMU not used in VMOS

## Debug Tools

**LLDB scripts**: `platform/rv64/script/debug/lldb_rv64.txt`

**Memory validation**:
```c
platform_mem_validate_critical();
platform_mem_print_layout();
```

**Register inspection**:
```
(lldb) register read a0 a1 a2 a3 a4 a5 a6 a7  // Arguments
(lldb) register read t0 t1 t2 t3 t4 t5 t6     // Temporaries
(lldb) register read sp ra pc                  // Stack, return, program counter
(lldb) register read sstatus                   // Supervisor status
```

**CSR Registers**:
```
sstatus - Supervisor status
sie     - Supervisor interrupt enable
sip     - Supervisor interrupt pending
stvec   - Supervisor trap vector
sscratch - Supervisor scratch
sepc    - Supervisor exception PC
scause  - Supervisor exception cause
```

## Known Issues

**OpenSBI Dependency**: Kernel must be loaded by OpenSBI, cannot boot standalone

**PLIC Complexity**: Platform-Level Interrupt Controller requires careful configuration

**DTB Parsing**: Device addresses passed via DTB in a0 register at boot

# VMOS Debugging Guide

## Quick Start

**Launch debug session:**
```bash
make debug-lldb PLATFORM=x64
```

**Manual debug workflow:**
```bash
# Terminal 1: Start QEMU with debug stub
make DEBUG=1 run PLATFORM=x64

# Terminal 2: Attach LLDB
lldb build/x64/kernel.elf -s script/debug/lldb_crashdump.txt
```

**Common breakpoints:**
```
(lldb) b kmain
(lldb) b kpanic
(lldb) b platform_irq_dispatch
```

## Build Modes

**DEBUG=1**: Combined debug build + QEMU debugging
- `-O1`: Minimal optimizations (easier to step through)
- `-g3`: Maximum debug info (macros, types)
- `-fno-omit-frame-pointer`: Enable stack traces
- `-DKDEBUG`: Enable debug checks
- QEMU flags: `-s -S -d guest_errors`

**DEBUG=0 (default)**: Release build
- `-O2`: Full optimizations
- `--strip-debug`: Remove symbols
- Zero overhead (debug code compiled out)

**Using KDEBUG in code:**
```c
#ifdef KDEBUG
  // Expensive validation (compiled out in release)
  platform_mem_validate_critical();
  validate_work_queue_integrity();
#endif

// Always checked (critical invariant)
KASSERT(work != NULL, "work is null");

// Only in debug builds (expensive)
KDEBUG_ASSERT(work_queue_is_valid(&kernel.work_live_head),
              "work queue corrupted");
```

## Crash Analysis

**Reading panic output:**
```
=== KERNEL PANIC ===
Test panic handler

Registers:
  RIP: 0x00000000002XXXXX
  RSP: 0x00000000023XXXX
  ...

Stack dump (64 bytes from SP):
  0x023XXXX: 00 00 00 00 ...

Last work transitions:
  0x023YYYY: 0 -> 1 @ 100ms
```

**Resolve crash address to source:**
```bash
script/debug/addr2line.sh build/x64/kernel.elf 0x200123
```

**Analyze binary sections:**
```bash
make debug-analyze PLATFORM=x64
make debug-symbols PLATFORM=x64
```

**Stack trace interpretation:**
```
(lldb) bt
frame #0: 0x200123 kernel`kpanic at kbase.c:45
frame #1: 0x201456 kernel`kmain_step at kernel.c:234
frame #2: 0x202789 kernel`kmain at kmain.c:67
```

## LLDB Workflows

**Load formatters:**
```
(lldb) command script import script/debug/lldb_formatters.py
```

**Pretty-print structures:**
```
(lldb) p kernel
kernel_t(now=1234ms, work_submit=0, work_live=2, work_ready=0, timers=5)

(lldb) p work
kwork_t(op=TIMER, state=LIVE, result=OK, flags=NONE)
```

**Work queue visualization:**
```
(lldb) command source script/debug/lldb_workqueue.txt
```

**Set conditional breakpoints:**
```
(lldb) b kmain_step
(lldb) breakpoint modify --condition 'kernel.work_live_count > 10'
```

**Memory watchpoints:**
```
(lldb) watchpoint set expression -- &kernel.work_live_head
```

## Memory Debugging

**Validate critical regions:**
```c
#ifdef KDEBUG
  platform_mem_validate_critical();  // Check page tables, .text, .rodata, .bss
  platform_mem_print_layout();       // Dump memory map
#endif
```

**Hex dumps:**
```c
kmem_dump(addr, 64);  // Hex dump with ASCII
kmem_dump_range("Stack", stack_start, stack_end);
```

**Guard bytes:**
```c
platform_mem_set_guard(buffer, 256);
// ... use buffer ...
KDEBUG_ASSERT(platform_mem_check_guard(buffer, 256), "buffer overflow");
```

**Detect corruption:**
```c
bool ok = kmem_validate_pattern(zero_region, 4096, 0x00);
KASSERT(ok, "BSS not zeroed");
```

## Platform-Specific Debugging

**x86 (x64/x32):**
- Page table dumps: `platform_mem_dump_pagetables()`
- IDT/GDT inspection: See `platform/x86/script/debug/lldb_x86.txt`
- Memory map: `platform/x86/doc/memory_map.md`

**ARM (arm64/arm32):**
- Exception vectors: See `platform/arm*/doc/memory_map.md`
- Translation tables: `platform_mem_dump_pagetables()`
- LLDB scripts: `platform/arm*/script/debug/lldb_arm*.txt`

**RISC-V (rv64/rv32):**
- CSR registers: See `platform/rv*/script/debug/lldb_rv*.txt`
- Trap handlers: See `platform/rv*/doc/memory_map.md`
- SBI interface: Platform-specific documentation

## Makefile Targets

```bash
make DEBUG=1 run PLATFORM=x64       # Debug build + run in QEMU
make debug-lldb PLATFORM=x64        # Automated LLDB attach
make debug-analyze PLATFORM=x64     # ELF section analysis
make debug-symbols PLATFORM=x64     # Symbol map generation
```

## Common Issues

**LLDB won't attach:**
- Ensure QEMU is running with `-s` flag (enabled by DEBUG=1)
- Check port 1234 is not in use: `lsof -i :1234`
- Try manual attach: `(lldb) gdb-remote localhost:1234`

**Symbols not found:**
- Verify debug build: `make DEBUG=1 PLATFORM=x64`
- Check ELF has debug info: `llvm-readelf -S build/x64/kernel.elf | grep debug`

**Formatters not working:**
- Import script: `command script import script/debug/lldb_formatters.py`
- Check Python path: Script must be relative to LLDB launch directory

**Work queue corruption:**
- Enable work history: Build with `DEBUG=1`
- Inspect transitions: `kdebug_dump_work_history()` in panic output
- Set breakpoint: `b kernel.c:transition_work_state`

## Performance Impact

**DEBUG=1 overhead:**
- ~2-3x slower execution (O1 vs O2 + KDEBUG checks)
- ~10-20x larger binary (debug symbols)
- Acceptable for development, use DEBUG=0 for benchmarks

**KDEBUG checks:**
- Memory validation: ~100µs per call
- Work queue validation: ~10µs per state transition
- All compiled out in release builds

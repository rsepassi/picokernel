# x64 Memory Debugging - Current Status

## Summary

Added comprehensive memory debugging instrumentation to the x64 platform and created lldb debugging scripts to investigate memory layout and potential corruption during initialization.

## What Was Added

### 1. Memory Debugging Utilities (`platform/x86/mem_debug.c`)

Already present in the codebase with these capabilities:

- **mem_dump()**: Hex dump of memory regions with ASCII representation
- **mem_dump_range()**: Dump memory by start/end address
- **mem_validate_pattern()**: Validate memory contains expected pattern
- **mem_ranges_overlap()**: Check for region overlaps
- **mem_dump_page_tables()**: Dump x86-64 page table hierarchy (PML4, PDPT, PD, PT)
- **mem_validate_critical_regions()**: Validate integrity of:
  - Page Tables (0x100000-0x105000)
  - .text section (0x200000-0x20D000)
  - .rodata section (0x20D000-0x20F000)
  - .bss section (0x20F000-0x23DC00)
- **mem_print_map()**: Print complete x86 memory map summary
- **mem_set_guard() / mem_check_guard()**: Memory canary support

### 2. Platform Initialization Instrumentation

Modified `platform/x86/platform_init.c` to call debugging functions at key points:

```c
// Early in initialization
mem_print_map();

// After device initialization
mem_validate_critical_regions();
mem_dump_page_tables();
```

This provides visibility into:
- Memory layout before and after device enumeration
- Page table contents during boot
- Detection of memory corruption in critical sections

### 3. LLDB Debugging Scripts

Created multiple debugging scripts in `/tmp/`:

#### `/tmp/lldb_memdebug.txt`
Comprehensive script with breakpoints at:
- `_start` (boot entry)
- `kmain` (kernel main)
- `platform_init` (platform initialization)
- `mem_print_map` (memory map printing)
- `mem_validate_critical_regions` (validation)
- `mem_dump_page_tables` (page table dump)

With automatic commands to examine:
- Registers (rip, rsp, rbp, rax)
- Page table hierarchy (PML4, PDPT, PD0, PD3, PT)
- Critical memory regions (.rodata, page tables)

#### `/tmp/lldb_simple.txt`
Simplified script focusing on:
- Breakpoint at `kmain`
- Watchpoint on .rodata section (256 bytes at 0x20D000)
- Automatic page table examination at breakpoints

## Current Issue: Memory Corruption Detected

### Symptoms

Running the x64 kernel shows **garbled printk() output**:

```
=== VMOS KMAIN ===

20                                52                                8 ...
```

Instead of expected text output like:
```
=== VMOS KMAIN ===

Initializing x32 platform...

[MEM] === x86 Memory Map ===
  ...
```

### Analysis

This corruption pattern indicates:
1. .rodata section (where format strings live) is being corrupted
2. Corruption happens early - before or during `platform_init()`
3. Numbers appearing suggest memory is being overwritten with random/incorrect data

### Documented History

From `MEMORY_MAP_x64.md`, this issue has been seen before:
- **Root cause was PCI BAR allocation overwriting QEMU-assigned BARs**
- MSI-X table writes went to wrong addresses
- Caused corruption in .rodata section
- Fixed by respecting QEMU's pre-assigned BAR addresses

However, the corruption is appearing again, suggesting:
1. The fix may have regressed
2. New code introduced similar issue
3. Different corruption source (not PCI-related)

### Next Steps for Investigation

1. **Use lldb with watchpoints**:
   ```bash
   # Start QEMU with GDB stub
   make run PLATFORM=x64 USE_PCI=1 DEBUG=1 &

   # Connect lldb and set watchpoint on .rodata
   lldb build/x64/kernel.elf
   (lldb) gdb-remote localhost:1234
   (lldb) watchpoint set expression -w write -s 4096 -- (void*)0x20D000
   (lldb) continue
   ```

   This will catch the exact instruction that corrupts .rodata.

2. **Examine page tables during corruption**:
   - Check if page table entries at 0x104000+ map .rodata correctly
   - Verify permissions are read-only (no write bit set)
   - Look for DMA or MMIO writes to physical addresses overlapping .rodata

3. **Verify PCI BAR allocation** (in case of regression):
   - Check `platform_virtio.c:allocate_pci_bars()`
   - Ensure it reads existing BARs before allocating
   - Verify MSI-X table addresses are in valid MMIO range (0xC0000000+)

4. **Check boot.S page table setup**:
   - Verify PT entries at 0x104000 have correct permissions
   - .text/.rodata should be present (0x01) not writable (0x03)
   - .bss should be writable (0x03)

5. **Add memory guards**:
   ```c
   // In platform_init, before device enumeration
   mem_set_guard((void*)0x20D000, 0xDEADBEEFDEADBEEF);

   // After device enumeration
   if (!mem_check_guard((void*)0x20D000, 0xDEADBEEFDEADBEEF)) {
       printk("CORRUPTION DETECTED!\n");
   }
   ```

## Memory Layout Reference

### x64 Physical Memory (128 MiB)

```
0x00000000 - 0x000FFFFF:  Low Memory / BIOS (1 MiB)
0x00100000 - 0x00104FFF:  Page Tables (20 KiB)
  - PML4 at 0x100000
  - PDPT at 0x101000
  - PD0 at 0x102000 (0-1GB)
  - PD3 at 0x103000 (3-4GB MMIO)
  - PT at 0x104000 (kernel 4KB pages)

0x00200000 - 0x0020CFFF:  .text (Read-only)
0x0020D000 - 0x0020EFFF:  .rodata (Read-only) ← CORRUPTION HERE
0x0020F000 - 0x0023DBFF:  .bss + stack (Read-Write)

0x0023DC00 - 0x07FFFFFF:  Free RAM (~126 MiB)

0xC0000000 - 0xCFFFFFFF:  PCI MMIO region (256 MiB)
0xFE000000 - 0xFEFFFFFF:  High MMIO (16 MiB)
  - 0xFEB00000: VirtIO MMIO devices
  - 0xFEC00000: IOAPIC
  - 0xFEE00000: Local APIC
```

## Tools Available

### Command-line debugging:
```bash
# Examine kernel sections
llvm-objdump -h build/x64/kernel.elf
llvm-nm -S -n build/x64/kernel.elf

# Disassemble .rodata
llvm-objdump -s --section=.rodata build/x64/kernel.elf

# Check structure sizes
echo 'p sizeof(platform_t)' | lldb build/x64/kernel.elf
```

### Memory debugging functions (callable from code):
- `mem_print_map()` - Print memory layout
- `mem_validate_critical_regions()` - Validate all sections
- `mem_dump_page_tables()` - Dump page table hierarchy
- `mem_dump(addr, len)` - Hex dump specific region
- `mem_set_guard()` / `mem_check_guard()` - Canary detection

## Files Modified

1. `platform/x86/platform_init.c`:
   - Added `#include "mem_debug.h"`
   - Added `mem_print_map()` call early in initialization
   - Added `mem_validate_critical_regions()` after device init
   - Added `mem_dump_page_tables()` for debugging

2. `/tmp/lldb_memdebug.txt`: Comprehensive lldb script
3. `/tmp/lldb_simple.txt`: Simplified watchpoint-focused script
4. `MEMORY_MAP_x64.md`: Documentation (already existed)
5. `X64_MEM_DEBUG_STATUS.md`: This file

## Conclusion

The x64 platform now has comprehensive memory debugging instrumentation that can:
- Print memory maps during initialization
- Validate critical memory regions
- Dump page table contents
- Detect corruption with watchpoints and canaries
- Provide detailed memory layout information

However, **active memory corruption is occurring in the .rodata section**, causing printk() output to be garbled. The lldb debugging scripts and watchpoints are set up to catch the exact moment of corruption, which is the critical next step in resolving this issue.

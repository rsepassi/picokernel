# VMOS Debugging Infrastructure Plan

This document outlines the debugging infrastructure roadmap for VMOS, focusing
on quick wins with zero runtime overhead in release builds.

## Design Principles

1. **Zero overhead in release builds** - All expensive debug features compiled out unless explicitly enabled
2. **Platform-agnostic core** - Common debugging tools work across all architectures
3. **Platform-specific extensions** - Architecture-specific details in platform directories
4. **LLDB-first workflow** - Primary debugger with good script support
5. **Quick wins prioritized** - High-impact, low-effort improvements first

## Current State

### Existing Infrastructure

**Build System:**
- `DEBUG=1` flag enables debug build mode (`-O1 -g3 -fno-omit-frame-pointer -DKDEBUG`) and QEMU debugging (`-s -S -d guest_errors`)
- `DEBUG=0` (default) compiles with `-O2` optimization, strips debug symbols
- Conditional debug compilation via `KDEBUG` flag

**Scripts (script/debug/):**
- `lldb_memdebug.txt` - Comprehensive LLDB script with memory validation breakpoints
- `lldb_simple.txt` - Simplified debugging setup
- `lldb_init.txt` - Basic initialization
- `addr2line.sh` - Resolve crash addresses to source:line
- `dump_symbols.sh` - Generate symbol map sorted by size
- `analyze_elf.sh` - Comprehensive ELF binary analysis
- `MEMORY_MAP_x64.md` - x64 memory layout documentation
- `X64_MEM_DEBUG_STATUS.md` - Memory debugging status and known issues

**Runtime Debugging:**
- `printk()` - Basic UART logging (NOT interrupt-safe)
- `KASSERT()` - Fatal assertions with file/line/message
- `KLOG()` - Timestamped logging
- `KDEBUG_ASSERT()`, `KDEBUG_LOG()`, `KDEBUG_VALIDATE()` - Conditional debug macros (compiled out in release)
- `kabort()` - Simple crash handler (infinite loop) - *will be replaced by enhanced `kpanic()`*

**Platform-Specific (x86 only):**
- `platform/x86/mem_debug.c/h` - Comprehensive memory validation suite:
  - `kmem_dump()` - Hex dumps with ASCII
  - `platform_mem_validate_critical()` - Validates page tables, .text, .rodata, .bss
  - `platform_mem_dump_pagetables()` - x64 page table hierarchy dump
  - `platform_mem_print_layout()` - Complete memory map
  - Guard byte support

### Key Gaps

1. ~~**No debug build mode**~~ ✅ RESOLVED - `DEBUG=1` enables `-O1 -g3 -DKDEBUG`
2. ~~**No symbol analysis tools**~~ ✅ RESOLVED - `addr2line.sh`, `dump_symbols.sh`, `analyze_elf.sh`
3. ~~**Poor panic output**~~ ✅ RESOLVED - `kpanic()` dumps registers, stack, work history
4. **Platform-specific memory debugging** - Only x86 has validation tools (Phase 4 TODO)
5. ~~**Manual LLDB workflow**~~ ✅ RESOLVED - Pretty-printers and automated scripts available
6. ~~**No conditional compilation**~~ ✅ RESOLVED - `KDEBUG` macros implemented

## Implementation Plan

### Phase 1: Build System Enhancements ⚠️ MOSTLY DONE (missing: debug-lldb target)

**Goal:** Add zero-overhead debug build mode with conditional compilation.

#### 1.1 Unified DEBUG Flag (Makefile) ✅ DONE

Add single debug control flag:
```makefile
DEBUG ?= 0
```

**DEBUG=0 (Release, default):**
- `-O2` - Full optimizations
- `-fomit-frame-pointer` - Smaller/faster code
- `--strip-debug` - Remove debug symbols
- No QEMU debug flags
- Production-ready binary

**DEBUG=1 (Debug):**
- `-O1` - Minimal optimizations (easier to step through)
- `-g3` - Maximum debug information (macros, types)
- `-fno-omit-frame-pointer` - Enable stack traces
- `-DKDEBUG` - Enable debug checks in code
- Keep all symbols in ELF
- QEMU flags: `-s -S -d guest_errors`
  - `-s`: GDB stub on port 1234
  - `-S`: Pause on startup (wait for debugger)
  - `-d guest_errors`: Log invalid guest operations

**Implementation:**
```makefile
ifeq ($(DEBUG),1)
  CFLAGS += -O1 -g3 -fno-omit-frame-pointer -DKDEBUG
  QEMU_DEBUG_FLAGS := -s -S -d guest_errors
else
  CFLAGS += -O2 -fomit-frame-pointer
  LDFLAGS += --strip-debug
  QEMU_DEBUG_FLAGS :=
endif
```

**Usage in code:**
```c
#ifdef KDEBUG
  // Expensive validation that's compiled out in release
  validate_work_queue_integrity();
  check_memory_guards();
#endif
```

#### 1.2 New Makefile Targets ⚠️ PARTIALLY DONE

**Implemented:**
- ✅ `make debug-analyze PLATFORM=<arch>` - Comprehensive ELF analysis
- ✅ `make debug-symbols PLATFORM=<arch>` - Generate symbol map

**Not implemented:**
- ❌ `make debug-lldb PLATFORM=<arch>` - Automated LLDB attachment (manual workflow: use `lldb_crashdump.txt` or `lldb_workqueue.txt` scripts instead)

**Current workflow:**
```bash
# Debug build + run QEMU with GDB stub
make DEBUG=1 run PLATFORM=x64

# Attach LLDB manually (in another terminal)
lldb build/x64/kernel.elf -s script/debug/lldb_crashdump.txt
```

---

### Phase 2: Platform-Agnostic Debug Scripts ✅ DONE

**Location:** `script/debug/` (architecture-independent tools)

#### 2.1 Symbol Resolution (addr2line.sh) ✅ DONE

```bash
#!/bin/bash
# Usage: addr2line.sh build/x64/kernel.elf 0x200123
KERNEL_ELF=$1
ADDRESS=$2
llvm-addr2line -e "$KERNEL_ELF" -f -C "$ADDRESS"
```

**Use case:** Quickly resolve crash addresses to source locations.

#### 2.2 Symbol Analysis (dump_symbols.sh) ✅ DONE

**Note:** Simplified - removed awk strtonum (not portable on macOS). Outputs raw llvm-nm format.

```bash
#!/bin/bash
# Generate sorted symbol map with sizes
KERNEL_ELF=$1
llvm-nm --print-size --size-sort "$KERNEL_ELF"
```

**Output:**
```
0000000000200000 0000000000004096 T _start
0000000000201000 0000000000012288 T kmain
```

#### 2.3 ELF Analysis (analyze_elf.sh) ✅ DONE

```bash
#!/bin/bash
# Comprehensive ELF analysis
KERNEL_ELF=$1

echo "=== Section Sizes ==="
llvm-objdump -h "$KERNEL_ELF" | grep -E '^\s+[0-9]+'

echo "=== Memory Usage ==="
llvm-size "$KERNEL_ELF"

echo "=== Top 20 Symbols by Size ==="
llvm-nm --print-size --size-sort "$KERNEL_ELF" | tail -20

echo "=== Entry Point ==="
llvm-readelf -h "$KERNEL_ELF" | grep Entry
```

#### 2.4 LLDB Pretty-Printers (lldb_formatters.py) ✅ DONE

**Purpose:** Human-readable structure dumps in LLDB.

**Implementation:** `script/debug/lldb_formatters.py`

**Features:**
- Pretty-printing for all work request types: `kwork_t`, `ktimer_req_t`, `krng_req_t`, `kblk_req_t`, `knet_recv_req_t`, `knet_send_req_t`
- Kernel state summary with queue depths and timer count
- Platform summary with device enumeration and capabilities
- Human-readable state, operation, and error code names

**Usage:**
```
(lldb) command script import script/debug/lldb_formatters.py
(lldb) p work
(kwork_t) $0 = kwork_t(op=TIMER, state=LIVE, result=OK, flags=NONE)
```

**Formatted structures:**
- `kernel_t` - Current time, work queue depths, timer count
- `kwork_t` - Operation, state, result, flags
- `ktimer_req_t` - Deadline with embedded work state
- `krng_req_t`, `kblk_req_t`, `knet_*_req_t` - Request-specific fields
- `platform_t` - VirtIO device presence (RNG, BLK with capacity, NET with MAC)

#### 2.5 Enhanced LLDB Scripts ✅ DONE

**lldb_crashdump.txt:** Complete crash analysis script

**Features:**
- Auto-connect to QEMU GDB stub
- Load pretty-printers automatically
- Dump all registers and call stack
- Show local variables and stack memory
- Display kernel and platform state
- Show work queue depths and timer state
- Display work transition history (KDEBUG builds)

**Usage:**
```bash
lldb build/x64/kernel.elf -s script/debug/lldb_crashdump.txt
```

**lldb_workqueue.txt:** Work queue visualization

**Features:**
- Current kernel time
- All work queues with queue walking
- Timer heap state with next deadline
- VirtIO device presence and capabilities
- CSPRNG state
- Work transition history (KDEBUG builds)

**Usage:**
```bash
lldb build/x64/kernel.elf -s script/debug/lldb_workqueue.txt
# Or after attaching:
command source script/debug/lldb_workqueue.txt
```

---

### Phase 3: Runtime Debug Enhancements ✅ DONE

**Goal:** Better panic output and conditional debugging.

#### 3.1 Enhanced Panic Handler (kernel/kbase.c) ✅ DONE

Replace `kabort()` with enhanced `kpanic()` that dumps state before halting:

```c
void kpanic(const char* msg) {
    printks("\n=== KERNEL PANIC ===\n");
    printks(msg);
    printks("\n");

    // Platform provides register dump
    platform_dump_registers();

    // Stack dump (64 bytes from current SP)
    platform_dump_stack(64);

#ifdef KDEBUG
    // Last completed work items (ring buffer)
    kdebug_dump_work_history();

    // Memory corruption indicators
    kdebug_check_stack_canary();
#endif

    // Halt: infinite loop or platform-specific halt instruction
    while (1) {
        __asm__ volatile("" ::: "memory");
    }
}
```

**Platform implementations** (platform/*/platform_debug.c):
```c
void platform_dump_registers(void) {
    // x64: RIP, RSP, RBP, RAX, RBX, etc.
    // ARM: PC, SP, LR, R0-R12
    // RISC-V: PC, SP, RA, A0-A7
}

void platform_dump_stack(u32 bytes) {
    // Read SP register
    // Dump bytes in hex
}
```

#### 3.2 Conditional Assertion Macros (kernel/kbase.h) ✅ DONE

```c
#ifdef KDEBUG
  #define KDEBUG_ASSERT(cond, msg) KASSERT(cond, msg)
  #define KDEBUG_LOG(msg) KLOG(msg)

  // Expensive validation
  #define KDEBUG_VALIDATE(func) func()
#else
  #define KDEBUG_ASSERT(cond, msg) ((void)0)
  #define KDEBUG_LOG(msg) ((void)0)
  #define KDEBUG_VALIDATE(func) ((void)0)
#endif
```

**Usage:**
```c
// Always checked (critical invariant)
KASSERT(work != NULL, "work is null");

// Only in debug builds (expensive)
KDEBUG_ASSERT(work_queue_is_valid(&kernel.work_live_head),
              "work queue corrupted");

KDEBUG_VALIDATE(validate_page_tables);
```

#### 3.3 Work Queue Debugging (kernel/kernel.c) ✅ DONE

Add debug instrumentation (compiled out in release):

```c
#ifdef KDEBUG
  #define WORK_HISTORY_SIZE 16

  typedef struct {
      kwork_t* work;
      kwork_state_t from_state;
      kwork_state_t to_state;
      u64 timestamp_ms;
  } work_transition_t;

  static work_transition_t g_work_history[WORK_HISTORY_SIZE];
  static u32 g_work_history_idx = 0;

  static void record_work_transition(kwork_t* work,
                                      kwork_state_t from,
                                      kwork_state_t to) {
      work_transition_t* t = &g_work_history[g_work_history_idx];
      t->work = work;
      t->from_state = from;
      t->to_state = to;
      t->timestamp_ms = kernel.now_ms;
      g_work_history_idx = (g_work_history_idx + 1) % WORK_HISTORY_SIZE;
  }

  void kdebug_dump_work_history(void) {
      printks("Last work transitions:\n");
      for (u32 i = 0; i < WORK_HISTORY_SIZE; i++) {
          work_transition_t* t = &g_work_history[i];
          if (t->work) {
              printks("  ");
              printk_hex64((u64)t->work);
              printks(": ");
              // Print state names
              printk_dec(t->from_state);
              printks(" -> ");
              printk_dec(t->to_state);
              printks(" @ ");
              printk_dec(t->timestamp_ms);
              printks("ms\n");
          }
      }
  }
#else
  #define record_work_transition(work, from, to) ((void)0)
  static inline void kdebug_dump_work_history(void) {}
#endif
```

**Integration in state machine:**
```c
static void transition_work_state(kwork_t* work, kwork_state_t new_state) {
    kwork_state_t old_state = work->state;
    KDEBUG_VALIDATE(validate_state_transition(old_state, new_state));
    record_work_transition(work, old_state, new_state);
    work->state = new_state;
}
```

---

### Phase 4: Platform-Specific Debug Tools ❌ TODO

**Goal:** Generalize memory debugging to all platforms.

#### 4.1 Platform-Agnostic Interface (kernel/mem_debug.h)

```c
#ifndef MEM_DEBUG_H
#define MEM_DEBUG_H

#include "kbase.h"

#ifdef KDEBUG

// Generic memory debugging (all platforms must implement)
void platform_mem_validate_critical(void);
void platform_mem_print_layout(void);
void platform_mem_dump_translation(uptr vaddr);

// Optional (platform may provide noop)
void platform_mem_dump_pagetables(void);
void platform_mem_set_guard(void* addr, u32 size);
bool platform_mem_check_guard(void* addr, u32 size);

// Helper utilities (implemented in kernel/mem_debug.c)
void kmem_dump(const void* addr, u32 len);
void kmem_dump_range(const char* label, const void* start, const void* end);
bool kmem_validate_pattern(const void* addr, u32 len, u8 pattern);
bool kmem_ranges_overlap(uptr a_start, u32 a_size, uptr b_start, u32 b_size);

#else
// Compiled out in release builds
static inline void platform_mem_validate_critical(void) {}
static inline void platform_mem_print_layout(void) {}
static inline void platform_mem_dump_translation(uptr vaddr) { (void)vaddr; }
static inline void platform_mem_dump_pagetables(void) {}
static inline void platform_mem_set_guard(void* addr, u32 size) { (void)addr; (void)size; }
static inline bool platform_mem_check_guard(void* addr, u32 size) { (void)addr; (void)size; return true; }
static inline void kmem_dump(const void* addr, u32 len) { (void)addr; (void)len; }
static inline void kmem_dump_range(const char* label, const void* start, const void* end) { (void)label; (void)start; (void)end; }
static inline bool kmem_validate_pattern(const void* addr, u32 len, u8 pattern) { (void)addr; (void)len; (void)pattern; return true; }
static inline bool kmem_ranges_overlap(uptr a_start, u32 a_size, uptr b_start, u32 b_size) { (void)a_start; (void)a_size; (void)b_start; (void)b_size; return false; }
#endif

#endif // MEM_DEBUG_H
```

#### 4.2 Platform Implementations

**Move platform/x86/mem_debug.c → platform/x86/platform_mem_debug.c**

Implement interface for each platform:

**x64 example (platform/x86/platform_mem_debug.c):**
```c
#include "kernel/mem_debug.h"
#include "platform_impl.h"

#ifdef KDEBUG

void platform_mem_validate_critical(void) {
    // Validate page tables at 0x100000
    // Validate .text, .rodata, .bss sections
    // Check for known corruption patterns
}

void platform_mem_print_layout(void) {
    printks("=== x64 Memory Layout ===\n");
    printks("Page Tables:  0x100000 - 0x105000\n");
    printks("Kernel .text: 0x200000 - 0x20D000\n");
    // ... etc
}

void platform_mem_dump_pagetables(void) {
    // x64-specific: walk PML4 → PDPT → PD → PT
}

#endif // KDEBUG
```

**ARM64 example (platform/arm64/platform_mem_debug.c):**
```c
void platform_mem_validate_critical(void) {
    // Validate exception vectors
    // Validate kernel sections
    // Check stack guard regions
}

void platform_mem_print_layout(void) {
    printks("=== ARM64 Memory Layout ===\n");
    printks("Kernel base:  0x40200000\n");
    // ... etc
}

void platform_mem_dump_pagetables(void) {
    // ARM64-specific: walk TTBR → level 1/2/3 tables
}
```

#### 4.3 Platform-Specific LLDB Scripts

Create `platform/*/script/debug/` directories:

**platform/x86/script/debug/lldb_x86.txt:**
```
# x86-specific debugging commands

# Define x86 register groups
register read rax rbx rcx rdx rsi rdi rbp rsp rip
register read cs ds es fs gs ss
register read cr0 cr2 cr3 cr4

# Page table walker
define walk_page_tables
  x/1xg 0x100000  # PML4
end

# IDT dump
define dump_idt
  # Parse IDT structure
end
```

**platform/arm64/script/debug/lldb_arm64.txt:**
```
# ARM64-specific debugging commands

# ARM registers
register read x0 x1 x2 x3 x4 x5 x6 x7
register read x29 x30 sp pc
register read cpsr

# Exception level
register read CurrentEL

# Translation table walker
define walk_ttbr
  # Walk from TTBR0_EL1
end
```

---

### Phase 5: Documentation ❌ TODO

#### 5.1 Debugging Guide (doc/debugging.md)

Create comprehensive debugging guide:

**Contents:**
1. **Quick Start**
   - `make DEBUG=1 run` workflow
   - Attaching LLDB: `make debug-lldb PLATFORM=x64`
   - Common breakpoints

2. **Build Modes**
   - `DEBUG=1`: Combined debug build + QEMU debugging
   - `KDEBUG` macro usage in code
   - Performance impact
   - QEMU debug flags explained

3. **Crash Analysis**
   - Reading panic output
   - Symbol resolution with `addr2line.sh`
   - Stack trace interpretation
   - Work queue state dump

4. **LLDB Workflows**
   - Loading formatters: `command script import script/debug/lldb_formatters.py`
   - Pretty-printing structures
   - Setting conditional breakpoints
   - Watchpoints on memory regions

5. **Memory Debugging**
   - Using `platform_mem_*` functions
   - Validating critical regions
   - Setting memory guards
   - Detecting corruption

6. **Platform-Specific Debugging**
   - See platform-specific docs in `platform/*/doc/`
   - x86: Page table dumps, IDT/GDT inspection
   - ARM: Exception vectors, translation tables
   - RISC-V: CSR registers, trap handlers

#### 5.2 Platform Memory Maps

**Each platform maintains its own documentation in `platform/*/doc/`:**

**platform/x86/doc/memory_map.md:**
- Physical memory layout for x64/x32
- Page table structure
- Kernel sections with addresses
- MMIO regions
- Known issues and quirks

**platform/arm64/doc/memory_map.md:**
- ARM64 memory layout
- DTB collision avoidance
- Platform-specific MMIO
- Exception vector locations

**platform/arm32/doc/memory_map.md:**
- ARM32 memory layout
- Platform-specific considerations

**platform/rv64/doc/memory_map.md:**
- RISC-V memory layout
- SBI interface regions

**platform/rv32/doc/memory_map.md:**
- RV32 memory layout

---

## Directory Structure After Implementation

```
vmos/
├── Makefile                    # Enhanced with DEBUG, debug-* targets
│
├── kernel/
│   ├── kbase.h                 # KDEBUG_* macros
│   ├── kbase.c                 # kpanic() implementation (replaces kabort)
│   ├── kernel.c                # Work queue debug instrumentation
│   └── mem_debug.h             # Platform-agnostic memory debug interface
│
├── platform/
│   ├── x86/
│   │   ├── platform_mem_debug.c  # x86 memory debugging
│   │   ├── platform_debug.c      # Register/stack dumps
│   │   ├── doc/
│   │   │   ├── memory_map.md     # x64/x32 memory layout
│   │   │   └── debugging.md      # x86-specific debug notes
│   │   └── script/debug/
│   │       ├── lldb_x86.txt
│   │       └── idt_gdt_dump.py
│   │
│   ├── arm64/
│   │   ├── platform_mem_debug.c
│   │   ├── platform_debug.c
│   │   ├── doc/
│   │   │   ├── memory_map.md
│   │   │   └── debugging.md
│   │   └── script/debug/
│   │       ├── lldb_arm64.txt
│   │       └── exception_vector_dump.py
│   │
│   ├── arm32/
│   │   ├── platform_mem_debug.c
│   │   ├── platform_debug.c
│   │   ├── doc/
│   │   │   └── memory_map.md
│   │   └── script/debug/
│   │       └── lldb_arm32.txt
│   │
│   ├── rv64/
│   │   ├── platform_mem_debug.c
│   │   ├── platform_debug.c
│   │   ├── doc/
│   │   │   └── memory_map.md
│   │   └── script/debug/
│   │       └── lldb_rv64.txt
│   │
│   └── rv32/
│       ├── platform_mem_debug.c
│       ├── platform_debug.c
│       ├── doc/
│       │   └── memory_map.md
│       └── script/debug/
│           └── lldb_rv32.txt
│
├── script/
│   └── debug/                  # Platform-agnostic tools
│       ├── addr2line.sh
│       ├── dump_symbols.sh
│       ├── analyze_elf.sh
│       ├── lldb_formatters.py
│       ├── lldb_crashdump.txt
│       ├── lldb_workqueue.txt
│       ├── lldb_init.txt       # (existing)
│       ├── lldb_memdebug.txt   # (existing)
│       └── lldb_simple.txt     # (existing)
│
└── doc/
    ├── debug_plan.md           # This file (implementation plan)
    ├── debugging.md            # User guide
    ├── api.md                  # (existing) API architecture
    ├── async.md                # (existing) Async work queue design
    └── virtio.md               # (existing) VirtIO implementation
```

---

### Verify Panic Handler
```c
// In user.c, trigger panic:
void kmain_usermain(void) {
    kpanic("Test panic handler");
}
```

Expected output:
```
=== KERNEL PANIC ===
Test panic handler

Registers:
  RIP: 0x00000000002XXXXX
  RSP: 0x00000000023XXXX
  RBP: 0x00000000023XXXX
  ...

Stack dump (64 bytes from SP):
  0x023XXXX: 00 00 00 00 00 00 00 00 ...
  ...

Last work transitions:
  0x023YYYY: 0 -> 1 @ 100ms
  0x023YYYY: 1 -> 2 @ 102ms
  ...
```

### Verify LLDB Formatters
```bash
make debug-lldb PLATFORM=x64
```

```
(lldb) command script import script/debug/lldb_formatters.py
(lldb) b kmain
(lldb) c
(lldb) p kernel
# Should show formatted output, not raw hex
```

---

## Future work

* Interactive debugging
* VirtIO ring state checks

### Interactive Debugging (QEMU Monitor Integration)
**Benefit:** Runtime state inspection/modification without external debugger

**Requirements:**
- Implement QEMU monitor protocol
- Custom commands for kernel structure inspection
- Hot-reload capabilities
- GDB remote protocol compatibility

---

## Resolved Decisions
- **Unified DEBUG flag:** Single `DEBUG` variable controls both build flags and QEMU debugging
- **Zero overhead in release:** Use `#ifdef KDEBUG` aggressively to compile out debug code
- **Platform structure:** Generic tools in `script/debug/`, platform-specific in `platform/*/script/debug/` and `platform/*/doc/`
- **Debug targets:** All debugging-related Makefile targets prefixed with `debug-*`
- **Panic handling:** Merged `kabort()` into `kpanic()` for single panic/halt function

---

## Success Metrics

**Phase 1 (Build System):**
- [x] Can build with `-O1 -g3` via `DEBUG=1`
- [x] Can analyze binary with `make debug-analyze`
- [x] Can generate symbols with `make debug-symbols`
- [ ] Can launch LLDB with `make debug-lldb PLATFORM=x64` (workaround: manual LLDB + scripts)
- [x] QEMU guest error logging enabled in debug mode

**Phase 2 (Debug Scripts):**
- [x] Can resolve crash addresses with `addr2line.sh`
- [x] LLDB pretty-prints all kernel structures via `lldb_formatters.py`
- [x] LLDB crash dump script (`lldb_crashdump.txt`)
- [x] LLDB work queue visualization (`lldb_workqueue.txt`)

**Phase 3 (Runtime):**
- [x] `kpanic()` dumps registers, stack, and debug info (when `KDEBUG` enabled)
- [x] Debug checks compiled out in release builds (verified with llvm-readelf)
- [x] Work queue history dumps in panic output (when `KDEBUG` enabled)

**Phase 4 (Platform-Specific - TODO):**
- [ ] Memory validation on all platforms (not just x86)
- [ ] Platform-specific LLDB scripts for all architectures

**Phase 5 (Documentation - TODO):**
- [ ] Platform-specific memory map documentation in `platform/*/doc/`
- [ ] Comprehensive debugging guide (`doc/debugging.md`)

---

## References

### Existing Documentation
- `doc/api.md` - API architecture and layer descriptions
- `doc/async.md` - Async work queue design
- `doc/virtio.md` - VirtIO implementation details
- `script/debug/MEMORY_MAP_x64.md` - x64 memory layout
- `script/debug/X64_MEM_DEBUG_STATUS.md` - Memory debugging status

### External Resources
- LLDB Python API: https://lldb.llvm.org/use/python-reference.html
- DWARF debugging format: http://dwarfstd.org/
- QEMU GDB protocol: https://www.qemu.org/docs/master/system/gdb.html
- UBSan implementation: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html

---

## Revision History

- 2025-11-01: Initial plan - quick wins, zero overhead, LLDB workflow
- 2025-11-01: **Completed Phases 1 (mostly), 2 (fully), 3 (fully)**
  - Phase 1: DEBUG=1 build mode, debug-analyze/debug-symbols targets (missing: debug-lldb)
  - Phase 2: Symbol scripts, LLDB pretty-printers, crash/workqueue analysis scripts
  - Phase 3: Enhanced kpanic(), KDEBUG macros, work queue history
  - Remaining: Phase 4 (platform-specific debug tools), Phase 5 (documentation)

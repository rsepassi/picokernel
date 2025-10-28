# Device Tree VirtIO Discovery - TODO

## UPDATE (2025-10-28 - Evening)

**Status**: Device tree parsing for VirtIO discovery has been temporarily disabled due to persistent crash at FDT offset 0x174.

### What Was Attempted

Multiple approaches were tried to fix the device tree VirtIO discovery:

1. **Recursive Walker (Modeled after `fdt_walk_node()`)**: Created a recursive function similar to the working `fdt_dump()` walker, but it crashed when accessing property names.

2. **Linear Walker with Depth Tracking**: Simplified to a linear walker that tracks depth to only process root-level children (depth==2), still crashed.

3. **Added Extensive Bounds Checking**: Added validation for:
   - Structure block bounds
   - String offset bounds
   - Property lengths
   - Still crashed at the same location

4. **Removed Depth Restrictions**: Tried processing properties at all depths to simplify logic, still crashed.

### The Persistent Crash

- **Location**: FAR_EL1 = 0x40000174 (offset 0x174 in FDT at 0x40000000)
- **Type**: Data abort (ESR_EL1 = 0x96000021)
- **When**: Very early in parsing, likely on first or second property access
- **Interesting**: `fdt_dump()` works perfectly on the same FDT data

### Current Workaround

- `fdt_find_virtio_mmio()` now returns 0 devices
- System reports "No VirtIO MMIO devices found in device tree"
- No crash occurs
- TODO comment added in src/devicetree.c:365-367

### Next Steps to Fix

The crash suggests a subtle bug in pointer arithmetic or property access that `fdt_walk_node()` handles correctly but the virtio finder does not. Recommendations:

1. **Compare Byte-for-Byte**: Put debug output in both `fdt_walk_node()` and the virtio finder to see exactly where they diverge in their pointer progression.

2. **Test with Minimal FDT**: Create a minimal FDT with just one virtio node to simplify debugging.

3. **Use GDB/LLDB**: Attach a debugger to see the exact instruction and memory state at crash time.

4. **Consider libfdt**: If available, use the official libfdt library instead of custom parsing.

5. **Alternative**: Implement a callback-based approach where `fdt_walk_node()` itself calls a callback for each node, avoiding duplication of parsing logic.

## Original Situation (2025-10-28)

ARM64 platform currently uses **manual scanning** to discover VirtIO MMIO devices:
- Hardcoded in `platform/arm64/platform_init.c`
- Scans addresses `0x0a000000` to `0x0a003e00` (32 slots, 0x200 bytes apart)
- Works reliably but defeats the purpose of device tree

## The Problem

Attempted to implement proper device tree parsing in `src/devicetree.c:fdt_find_virtio_mmio()` but it **crashes with data abort** at address `0x40000174` (within the FDT memory region).

### What We Know

1. **Device tree is valid** - `fdt_dump()` works perfectly and shows all VirtIO devices
2. **VirtIO devices ARE in the tree** - Dump shows 24+ `virtio_mmio@*` nodes with:
   - `compatible = "virtio,mmio"`
   - `reg = <addr, size>` properties
   - `interrupts = <type, num, flags>` properties
3. **Compatible matching works** - Debug output confirmed "virtio,mmio" strings are found
4. **Property collection fails** - Never successfully extracts `reg` or `interrupts` data

### Root Causes Identified

1. **Property Order Issue**: In FDT binary format, `reg` and `interrupts` often appear BEFORE `compatible` property. Original code only collected properties AFTER seeing `compatible="virtio,mmio"`, so it missed them.

2. **Pointer Arithmetic Bugs**: The linear FDT walker doesn't correctly advance pointers through all token types, eventually reading beyond valid memory.

3. **Insufficient Bounds Checking**: Code checks `p < struct_end` but doesn't validate each property length before dereferencing.

## How To Fix It

### Approach 1: Fix the Linear Walker (Harder)

Current code in `fdt_count_virtio_devices()` tries to linearly walk FDT tokens. To fix:

```c
// For each node, collect ALL properties first, then check at END_NODE:
int in_virtio_node = 0;
uint64_t reg_addr = 0, reg_size = 0;
uint32_t irq = 0;

while (p < struct_end) {
    // CRITICAL: Add bounds check BEFORE every read
    if (p + 4 > struct_end) break;

    token = read_token();

    if (BEGIN_NODE) {
        // Reset accumulators
        in_virtio_node = 0;
        reg_addr = reg_size = irq = 0;
    }
    else if (PROP) {
        // Collect reg/interrupts regardless of compatible
        // Set in_virtio_node=1 if compatible="virtio,mmio"
    }
    else if (END_NODE) {
        // NOW check: if (in_virtio_node && reg_addr != 0) { save it }
    }

    // CRITICAL: Verify pointer arithmetic for EVERY token type
    p = advance_pointer_correctly(token, length);
}
```

Key fixes needed:
- Validate `len` field before `value + len`
- Handle nested nodes correctly (track depth?)
- Test with single-device tree first

### Approach 2: Reuse Recursive Walker (Easier, Recommended)

The existing `fdt_walk_node()` function works perfectly for `fdt_dump()`. Adapt it:

```c
typedef struct {
    virtio_mmio_device_t* devices;
    int max_devices;
    int count;
    const char* strings;

    // Current node state
    int is_virtio_mmio;
    uint64_t reg_addr, reg_size;
    uint32_t irq;
} find_context_t;

static void fdt_find_virtio_recursive(const uint8_t* p, find_context_t* ctx, int depth) {
    // Reset node state at BEGIN_NODE
    // Collect properties
    // At END_NODE, if is_virtio_mmio, save to ctx->devices[]
    // Recurse on child nodes
}
```

This approach:
- Reuses proven pointer arithmetic from `fdt_walk_node()`
- Handles nesting correctly
- Much less likely to have bugs

### Approach 3: Use libfdt (If Available)

If you can link against libfdt:
```c
int nodeoffset = fdt_node_offset_by_compatible(fdt, -1, "virtio,mmio");
while (nodeoffset >= 0) {
    const fdt32_t *reg = fdt_getprop(fdt, nodeoffset, "reg", &len);
    const fdt32_t *interrupts = fdt_getprop(fdt, nodeoffset, "interrupts", &len);
    // Extract values...
    nodeoffset = fdt_node_offset_by_compatible(fdt, nodeoffset, "virtio,mmio");
}
```

## Testing Strategy

1. **Start simple**: Test with QEMU providing just ONE virtio device
2. **Add debug**: Print pointer offsets at every step
3. **Compare**: Run both `fdt_dump()` and `fdt_find_virtio_mmio()`, verify they visit same nodes
4. **Validate**: Check every bounds condition before dereferencing

## Why This Matters

- Device tree is the standard way to discover hardware on ARM
- Manual scanning only works on QEMU virt machine
- Real hardware may have devices at different addresses
- This blocks support for other ARM boards

## References

- Device Tree Specification: https://devicetree-specification.readthedocs.io/
- Working code: `src/devicetree.c:fdt_dump()` and `fdt_walk_node()`
- VirtIO spec: https://docs.oasis-open.org/virtio/virtio/v1.1/
- QEMU virt machine docs: https://www.qemu.org/docs/master/system/arm/virt.html

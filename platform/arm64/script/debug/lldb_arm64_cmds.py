"""
ARM64-specific LLDB debugging commands
Helper functions for ARM64 translation table walking, exception vector inspection
"""

import lldb

def walk_ttbr(debugger, command, result, internal_dict):
    """Walk ARM64 translation tables for a given virtual address"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()

    if not command:
        result.AppendMessage("Usage: walk_ttbr <virtual_address>")
        return

    try:
        vaddr = int(command, 0)
    except ValueError:
        result.AppendMessage(f"Invalid address: {command}")
        return

    result.AppendMessage(f"\n=== ARM64 Translation Table Walk for 0x{vaddr:016x} ===\n")

    # Extract indices (assuming 4KB granule, 3-level tables for 39-bit VA)
    l0_idx = (vaddr >> 39) & 0x1FF
    l1_idx = (vaddr >> 30) & 0x1FF
    l2_idx = (vaddr >> 21) & 0x1FF
    l3_idx = (vaddr >> 12) & 0x1FF
    offset = vaddr & 0xFFF

    result.AppendMessage(f"  L0[{l0_idx}] L1[{l1_idx}] L2[{l2_idx}] L3[{l3_idx}] Offset: 0x{offset:03x}")

    # Note: Would need to read TTBR0_EL1 or TTBR1_EL1 to get table base
    # For now, just show the structure
    result.AppendMessage("\n  Translation table walking requires TTBR register access")
    result.AppendMessage("  (Reading TTBR0_EL1/TTBR1_EL1 system registers)")
    result.AppendMessage("")


def dump_exception_vectors(debugger, command, result, internal_dict):
    """Dump ARM64 exception vector table"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()

    result.AppendMessage("\n=== ARM64 Exception Vector Table ===\n")

    # Exception vectors are typically at a known location or in VBAR_EL1
    result.AppendMessage("Exception vector inspection not fully implemented")
    result.AppendMessage("(Would read VBAR_EL1 register and dump vector entries)")
    result.AppendMessage("")

    result.AppendMessage("ARM64 exception vector layout (each 128 bytes):")
    result.AppendMessage("  0x000: Current EL with SP0, Synchronous")
    result.AppendMessage("  0x080: Current EL with SP0, IRQ")
    result.AppendMessage("  0x100: Current EL with SP0, FIQ")
    result.AppendMessage("  0x180: Current EL with SP0, SError")
    result.AppendMessage("  0x200: Current EL with SPx, Synchronous")
    result.AppendMessage("  0x280: Current EL with SPx, IRQ")
    result.AppendMessage("  0x300: Current EL with SPx, FIQ")
    result.AppendMessage("  0x380: Current EL with SPx, SError")
    result.AppendMessage("  0x400: Lower EL (AArch64), Synchronous")
    result.AppendMessage("  0x480: Lower EL (AArch64), IRQ")
    result.AppendMessage("  0x500: Lower EL (AArch64), FIQ")
    result.AppendMessage("  0x580: Lower EL (AArch64), SError")
    result.AppendMessage("  0x600: Lower EL (AArch32), Synchronous")
    result.AppendMessage("  0x680: Lower EL (AArch32), IRQ")
    result.AppendMessage("  0x700: Lower EL (AArch32), FIQ")
    result.AppendMessage("  0x780: Lower EL (AArch32), SError")
    result.AppendMessage("")


# Register commands when script is loaded
def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f lldb_arm64_cmds.walk_ttbr walk_ttbr')
    debugger.HandleCommand('command script add -f lldb_arm64_cmds.dump_exception_vectors dump_vectors')
    print("ARM64 debugging commands loaded")

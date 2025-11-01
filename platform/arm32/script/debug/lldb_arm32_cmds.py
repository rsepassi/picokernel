"""
ARM32-specific LLDB debugging commands
Helper functions for ARM32 page table walking, exception vector inspection
"""

import lldb

def walk_page_tables(debugger, command, result, internal_dict):
    """Walk ARM32 page tables for a given virtual address"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()

    if not command:
        result.AppendMessage("Usage: walk_pt <virtual_address>")
        return

    try:
        vaddr = int(command, 0)
    except ValueError:
        result.AppendMessage(f"Invalid address: {command}")
        return

    result.AppendMessage(f"\n=== ARM32 Page Table Walk for 0x{vaddr:08x} ===\n")

    # Extract indices (assuming 4KB pages, 2-level paging)
    l1_idx = (vaddr >> 20) & 0xFFF  # 12 bits for L1
    l2_idx = (vaddr >> 12) & 0xFF   # 8 bits for L2
    offset = vaddr & 0xFFF

    result.AppendMessage(f"  L1[{l1_idx}] L2[{l2_idx}] Offset: 0x{offset:03x}")

    # Note: Would need to read TTBR0/TTBR1 to get table base
    result.AppendMessage("\n  Page table walking requires TTBR register access")
    result.AppendMessage("  (Reading TTBR0/TTBR1 system registers)")
    result.AppendMessage("")


def dump_exception_vectors(debugger, command, result, internal_dict):
    """Dump ARM32 exception vector table"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()

    result.AppendMessage("\n=== ARM32 Exception Vector Table ===\n")

    # ARM32 vectors are typically at 0x00000000 or 0xFFFF0000 (high vectors)
    result.AppendMessage("Exception vector inspection not fully implemented")
    result.AppendMessage("(Would read VBAR register or check at 0x00000000/0xFFFF0000)")
    result.AppendMessage("")

    result.AppendMessage("ARM32 exception vector layout (each 4 bytes):")
    result.AppendMessage("  0x00: Reset")
    result.AppendMessage("  0x04: Undefined Instruction")
    result.AppendMessage("  0x08: Supervisor Call (SWI/SVC)")
    result.AppendMessage("  0x0C: Prefetch Abort")
    result.AppendMessage("  0x10: Data Abort")
    result.AppendMessage("  0x14: Reserved")
    result.AppendMessage("  0x18: IRQ")
    result.AppendMessage("  0x1C: FIQ")
    result.AppendMessage("")


# Register commands when script is loaded
def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f lldb_arm32_cmds.walk_page_tables walk_pt')
    debugger.HandleCommand('command script add -f lldb_arm32_cmds.dump_exception_vectors dump_vectors')
    print("ARM32 debugging commands loaded")

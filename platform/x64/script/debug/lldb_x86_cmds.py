"""
x86-specific LLDB debugging commands
Helper functions for x86 page table walking, IDT/GDT inspection
"""

import lldb

def walk_page_tables(debugger, command, result, internal_dict):
    """Walk x86-64 page tables for a given virtual address"""
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

    result.AppendMessage(f"\n=== x86-64 Page Table Walk for 0x{vaddr:016x} ===\n")

    # Extract indices
    pml4_idx = (vaddr >> 39) & 0x1FF
    pdpt_idx = (vaddr >> 30) & 0x1FF
    pd_idx = (vaddr >> 21) & 0x1FF
    pt_idx = (vaddr >> 12) & 0x1FF
    offset = vaddr & 0xFFF

    result.AppendMessage(f"  PML4[{pml4_idx}] PDPT[{pdpt_idx}] PD[{pd_idx}] PT[{pt_idx}] Offset: 0x{offset:03x}")

    # Read CR3 to get PML4 base (or use known location)
    # For VMOS, PML4 is at 0x100000
    pml4_base = 0x100000

    # Read PML4 entry
    pml4_entry_addr = pml4_base + (pml4_idx * 8)
    error = lldb.SBError()
    pml4_entry = process.ReadUnsignedFromMemory(pml4_entry_addr, 8, error)

    if error.Success():
        result.AppendMessage(f"\n  PML4[{pml4_idx}] @ 0x{pml4_entry_addr:016x} = 0x{pml4_entry:016x}")

        if pml4_entry & 1:
            result.AppendMessage(f"    Present: Yes")
            result.AppendMessage(f"    Writable: {'Yes' if pml4_entry & 2 else 'No'}")
            result.AppendMessage(f"    User: {'Yes' if pml4_entry & 4 else 'No'}")
            result.AppendMessage(f"    Physical Address: 0x{pml4_entry & 0xFFFFFFFFFF000:012x}")

            # Continue walking PDPT, PD, PT...
            # (Full implementation would continue here)
        else:
            result.AppendMessage(f"    Not Present")
    else:
        result.AppendMessage(f"  Error reading PML4 entry: {error}")

    result.AppendMessage("")


def dump_idt(debugger, command, result, internal_dict):
    """Dump the Interrupt Descriptor Table"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()

    result.AppendMessage("\n=== x86 IDT Dump ===\n")
    result.AppendMessage("IDT inspection not fully implemented")
    result.AppendMessage("(Would read IDTR register and dump IDT entries)\n")


def dump_gdt(debugger, command, result, internal_dict):
    """Dump the Global Descriptor Table"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()

    result.AppendMessage("\n=== x86 GDT Dump ===\n")
    result.AppendMessage("GDT inspection not fully implemented")
    result.AppendMessage("(Would read GDTR register and dump GDT entries)\n")


# Register commands when script is loaded
def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f lldb_x86_cmds.walk_page_tables walk_pt')
    debugger.HandleCommand('command script add -f lldb_x86_cmds.dump_idt dump_idt')
    debugger.HandleCommand('command script add -f lldb_x86_cmds.dump_gdt dump_gdt')
    print("x86 debugging commands loaded")

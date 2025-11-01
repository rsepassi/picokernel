"""
RISC-V 32-specific LLDB debugging commands
Helper functions for RV32 page table walking, CSR inspection
"""

import lldb

def walk_page_tables(debugger, command, result, internal_dict):
    """Walk RISC-V SV32 page tables for a given virtual address"""
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

    result.AppendMessage(f"\n=== RISC-V 32 Page Table Walk for 0x{vaddr:08x} ===\n")

    # Extract VPN fields (SV32: 2-level paging)
    vpn1 = (vaddr >> 22) & 0x3FF  # VPN[1] - 10 bits
    vpn0 = (vaddr >> 12) & 0x3FF  # VPN[0] - 10 bits
    offset = vaddr & 0xFFF

    result.AppendMessage(f"  VPN[1]={vpn1} VPN[0]={vpn0} Offset=0x{offset:03x}")
    result.AppendMessage("")

    # Note: Would need to read satp CSR to get page table base
    result.AppendMessage("  Page table walking requires satp CSR access")
    result.AppendMessage("  (satp contains mode, ASID, and PPN of root page table)")
    result.AppendMessage("")


def dump_csr(debugger, command, result, internal_dict):
    """Dump important RISC-V CSR registers"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()

    result.AppendMessage("\n=== RISC-V CSR Dump ===\n")

    # List of important CSRs
    csrs = [
        ("mstatus", "Machine Status"),
        ("mie", "Machine Interrupt Enable"),
        ("mip", "Machine Interrupt Pending"),
        ("mepc", "Machine Exception PC"),
        ("mcause", "Machine Cause"),
        ("mtval", "Machine Trap Value"),
        ("sstatus", "Supervisor Status"),
        ("sie", "Supervisor Interrupt Enable"),
        ("sip", "Supervisor Interrupt Pending"),
        ("sepc", "Supervisor Exception PC"),
        ("scause", "Supervisor Cause"),
        ("stval", "Supervisor Trap Value"),
        ("satp", "Supervisor Address Translation"),
    ]

    result.AppendMessage("CSR register reading depends on privilege level")
    result.AppendMessage("Use 'register read <csr_name>' to read individual CSRs")
    result.AppendMessage("")

    for csr_name, csr_desc in csrs:
        result.AppendMessage(f"  {csr_name:12} - {csr_desc}")

    result.AppendMessage("")


def decode_satp(debugger, command, result, internal_dict):
    """Decode the satp register value (RV32 version)"""
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()

    if not command:
        result.AppendMessage("Usage: decode_satp <satp_value>")
        result.AppendMessage("Example: decode_satp 0x80000000")
        return

    try:
        satp_value = int(command, 0)
    except ValueError:
        result.AppendMessage(f"Invalid satp value: {command}")
        return

    result.AppendMessage(f"\n=== Decoding satp (RV32): 0x{satp_value:08x} ===\n")

    # Extract fields (RV32)
    mode = (satp_value >> 31) & 0x1
    asid = (satp_value >> 22) & 0x1FF
    ppn = satp_value & 0x3FFFFF

    mode_names = {
        0: "Bare (no translation)",
        1: "SV32 (32-bit virtual)",
    }

    result.AppendMessage(f"  Mode: {mode} ({mode_names.get(mode, 'Reserved')})")
    result.AppendMessage(f"  ASID: {asid}")
    result.AppendMessage(f"  PPN:  0x{ppn:06x}")
    result.AppendMessage(f"  Physical Address of Root Page Table: 0x{ppn << 12:08x}")
    result.AppendMessage("")


# Register commands when script is loaded
def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f lldb_rv32_cmds.walk_page_tables walk_pt')
    debugger.HandleCommand('command script add -f lldb_rv32_cmds.dump_csr dump_csr')
    debugger.HandleCommand('command script add -f lldb_rv32_cmds.decode_satp decode_satp')
    print("RISC-V 32 debugging commands loaded")

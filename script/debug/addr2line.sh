#!/bin/bash
# addr2line.sh - Resolve crash addresses to source locations
# Usage: addr2line.sh build/x64/kernel.elf 0x200123

set -e

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <kernel.elf> <address>" >&2
    echo "Example: $0 build/x64/kernel.elf 0x200123" >&2
    exit 1
fi

KERNEL_ELF=$1
ADDRESS=$2

if [ ! -f "$KERNEL_ELF" ]; then
    echo "Error: Kernel ELF file not found: $KERNEL_ELF" >&2
    exit 1
fi

# -e: executable file
# -f: show function names
# -C: demangle C++ symbols
llvm-addr2line -e "$KERNEL_ELF" -f -C "$ADDRESS"

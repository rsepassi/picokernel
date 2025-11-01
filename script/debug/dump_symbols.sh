#!/bin/bash
# dump_symbols.sh - Generate sorted symbol map with sizes
# Usage: dump_symbols.sh build/x64/kernel.elf > symbols.txt

set -e

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <kernel.elf>" >&2
    echo "Example: $0 build/x64/kernel.elf > symbols.txt" >&2
    exit 1
fi

KERNEL_ELF=$1

if [ ! -f "$KERNEL_ELF" ]; then
    echo "Error: Kernel ELF file not found: $KERNEL_ELF" >&2
    exit 1
fi

# --print-size: show symbol sizes
# --size-sort: sort by size (smallest first)
# Output format: address size(hex) type name
llvm-nm --print-size --size-sort "$KERNEL_ELF"

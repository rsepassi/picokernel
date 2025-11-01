#!/bin/bash
# analyze_elf.sh - Comprehensive ELF binary analysis
# Usage: analyze_elf.sh build/x64/kernel.elf

set -e

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <kernel.elf>" >&2
    echo "Example: $0 build/x64/kernel.elf" >&2
    exit 1
fi

KERNEL_ELF=$1

if [ ! -f "$KERNEL_ELF" ]; then
    echo "Error: Kernel ELF file not found: $KERNEL_ELF" >&2
    exit 1
fi

echo "=== ELF Analysis: $KERNEL_ELF ==="
echo ""

echo "=== Section Sizes ==="
llvm-objdump -h "$KERNEL_ELF" | grep -E '^\s+[0-9]+'
echo ""

echo "=== Memory Usage ==="
llvm-size "$KERNEL_ELF"
echo ""

echo "=== Top 20 Symbols by Size ==="
llvm-nm --print-size --size-sort "$KERNEL_ELF" | tail -20
echo ""

echo "=== Entry Point ==="
llvm-readelf -h "$KERNEL_ELF" | grep Entry
echo ""

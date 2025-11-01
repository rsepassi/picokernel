#!/bin/bash
set -e

# Usage: ./script/flake.sh <platform> <use_pci>
# Example: ./script/flake.sh arm64 1

if [ -z "$1" ]; then
    echo "Usage: $0 <platform> <use_pci>"
    echo "  platform: arm64, arm32, x64, x32, rv64, rv32"
    echo "  use_pci: 0 (MMIO) or 1 (PCI)"
    echo "  Example: $0 arm64 1"
    exit 1
fi

if [ -z "$2" ]; then
    echo "Usage: $0 <platform> <use_pci>"
    echo "  platform: arm64, arm32, x64, x32, rv64, rv32"
    echo "  use_pci: 0 (MMIO) or 1 (PCI)"
    echo "  Example: $0 arm64 1"
    exit 1
fi

PLATFORM=$1
USE_PCI=$2

TRANSPORT="MMIO"
if [ "$USE_PCI" = "1" ]; then
    TRANSPORT="PCI"
fi

echo "=== Flake test: $PLATFORM with $TRANSPORT (10 runs) ==="

FAILED=0

for i in 1 2 3 4 5 6 7 8 9 10; do
    echo "[$i/10] Testing..."

    if make test PLATFORM="$PLATFORM" USE_PCI="$USE_PCI" 2>&1 | grep -q "Overall: PASS"; then
        echo "[$i/10] ✓ PASSED"
    else
        FAILED=$((FAILED + 1))
        echo "[$i/10] ✗ FAILED"
    fi
done

echo "=== Results: $((10 - FAILED))/10 passed ==="

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi

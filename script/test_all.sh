#!/bin/bash
set -e

# Run tests for all platforms and transports

echo "=== Running tests for all platforms and transports ==="
echo ""

FAILED=0
PASSED=0

for platform in arm64 arm32 x64 rv64 rv32; do
    for use_pci in 0 1; do
        transport="MMIO"
        if [ "$use_pci" = "1" ]; then
            transport="PCI"
        fi

        echo ">>> Testing $platform with $transport..."

        if make test PLATFORM="$platform" USE_PCI="$use_pci"; then
            PASSED=$((PASSED + 1))
            echo "✓ $platform ($transport) PASSED"
        else
            FAILED=$((FAILED + 1))
            echo "✗ $platform ($transport) FAILED"
        fi

        echo ""
    done
done

echo "=== Test Summary ==="
echo "Passed: $PASSED / 10"
echo "Failed: $FAILED / 10"

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi

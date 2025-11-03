#!/bin/bash
set -e

# Usage: ./script/run_test.sh <platform> <use_pci>
# Example: ./script/run_test.sh arm64 1

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

# Generate random port in the range 49152-65535
PORT=$((49152 + RANDOM % 16384))

# Create log directory with platform, transport type, and timestamp
TIMESTAMP=$(date '+%Y%m%d-%H%M%S')
TRANSPORT="mmio"
if [ "$USE_PCI" = "1" ]; then
    TRANSPORT="pci"
fi

LOGDIR="/tmp/vmos-logs/${PLATFORM}-${TRANSPORT}-${TIMESTAMP}"
mkdir -p "$LOGDIR"

TIMEOUT=2

echo "=== VMOS Test Suite ==="
echo "Platform: $PLATFORM"
echo "Transport: $TRANSPORT"
echo "Port: $PORT"
echo "Log directory: $LOGDIR"
echo ""

# Run make run with timeout in background, capturing output
echo "Starting QEMU..."
timeout $TIMEOUT make run PLATFORM="$PLATFORM" USE_PCI="$USE_PCI" PORT="$PORT" > "$LOGDIR/qemu.log" 2>&1 &
QEMU_PID=$!

# Give QEMU a moment to start (send_udp.sh starts immediately per requirements)
sleep 0.1

# Run send_udp.sh in parallel with timeout, capturing output
echo "Starting UDP echo client..."
timeout $TIMEOUT ./script/send_udp.sh 127.0.0.1 "$PORT" > "$LOGDIR/send_udp.log" 2>&1 &
UDP_PID=$!

# Wait for both processes to complete
wait $QEMU_PID 2>/dev/null || true
wait $UDP_PID 2>/dev/null || true

echo "Test run complete. Analyzing results..."
echo ""

# Parse logs and generate results.txt
RESULTS_FILE="$LOGDIR/results.txt"

{
    echo "=== VMOS Test Results ==="
    echo "Platform: $PLATFORM"
    echo "Transport: $TRANSPORT"
    echo "Port: $PORT"
    echo "Date: $(date '+%Y-%m-%d %H:%M:%S')"
    echo ""
} > "$RESULTS_FILE"

# Track overall pass/fail
OVERALL_PASS=1

# Helper function to check and log test result
check_test() {
    local test_name="$1"
    local log_file="$2"
    local pattern="$3"
    local invert="${4:-0}"  # If 1, test passes if pattern NOT found

    if [ "$invert" = "1" ]; then
        if ! grep -q "$pattern" "$log_file" 2>/dev/null; then
            echo "[PASS] $test_name" >> "$RESULTS_FILE"
        else
            echo "[FAIL] $test_name" >> "$RESULTS_FILE"
            OVERALL_PASS=0
        fi
    else
        if grep -q "$pattern" "$log_file" 2>/dev/null; then
            echo "[PASS] $test_name" >> "$RESULTS_FILE"
        else
            echo "[FAIL] $test_name" >> "$RESULTS_FILE"
            OVERALL_PASS=0
        fi
    fi
}

# Test 1: Build
if [ -f "build/$PLATFORM/kernel.elf" ]; then
    echo "[PASS] Build" >> "$RESULTS_FILE"
else
    echo "[FAIL] Build" >> "$RESULTS_FILE"
    OVERALL_PASS=0
fi

# Test 2: Startup
check_test "Startup" "$LOGDIR/qemu.log" "=== VMOS KMAIN ==="

# Test 3: Device Discovery - check for all three devices
if grep -q "Found.*RNG" "$LOGDIR/qemu.log" && \
   grep -q "Found.*[Bb]lock\|Found.*BLK" "$LOGDIR/qemu.log" && \
   grep -q "Found.*[Nn]et\|Found.*NET" "$LOGDIR/qemu.log"; then
    echo "[PASS] Device Discovery (RNG, Block, Net)" >> "$RESULTS_FILE"
else
    echo "[FAIL] Device Discovery (RNG, Block, Net)" >> "$RESULTS_FILE"
    OVERALL_PASS=0
fi

# Test 5: RNG Request
check_test "RNG Request" "$LOGDIR/qemu.log" "\[TEST PASS\] RNG"

# Test 6: Block Device Test
check_test "Block Device Test" "$LOGDIR/qemu.log" "\[TEST PASS\] Block Device"

# Test 7: Network - Received UDP packet
check_test "Network Received" "$LOGDIR/qemu.log" "Received UDP packet"

# Test 8: Network - Sent UDP response
check_test "Network Sent" "$LOGDIR/qemu.log" "Sent UDP response"

# Test 9: Network - Echo response received by client (nc prints the echoed message)
check_test "Network Echo Response" "$LOGDIR/send_udp.log" "hello from packet"

# Test 10: Network Echo Test Pass marker
check_test "Network Echo Test" "$LOGDIR/qemu.log" "\[TEST PASS\] Network Echo"

# Overall result
{
    echo ""
    if [ "$OVERALL_PASS" = "1" ]; then
        echo "Overall: PASS"
    else
        echo "Overall: FAIL"
    fi
} >> "$RESULTS_FILE"

# Display results
cat "$RESULTS_FILE"

# Exit with appropriate code
if [ "$OVERALL_PASS" = "1" ]; then
    exit 0
else
    exit 1
fi

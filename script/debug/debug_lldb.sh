#!/bin/bash
# Helper script for automated LLDB debugging workflow
# Usage: debug_lldb.sh <kernel.elf> <platform> <use_pci> <drive> <port>

set -e

if [ $# -lt 5 ]; then
    echo "Usage: $0 <kernel.elf> <platform> <use_pci> <drive> <port>"
    exit 1
fi

KERNEL_ELF="$1"
PLATFORM="$2"
USE_PCI="$3"
DRIVE="$4"
PORT="$5"

# Temporary file to store QEMU PID
QEMU_PID_FILE="/tmp/vmos_qemu_$$.pid"

# Cleanup function
cleanup() {
    if [ -f "$QEMU_PID_FILE" ]; then
        QEMU_PID=$(cat "$QEMU_PID_FILE")
        if kill -0 "$QEMU_PID" 2>/dev/null; then
            echo "Cleaning up QEMU (PID: $QEMU_PID)..."
            kill "$QEMU_PID" 2>/dev/null || true
            # Give it a moment to die gracefully
            sleep 0.5
            # Force kill if still alive
            kill -9 "$QEMU_PID" 2>/dev/null || true
        fi
        rm -f "$QEMU_PID_FILE"
    fi
}

# Register cleanup on exit
trap cleanup EXIT INT TERM

# Start QEMU in background via make run
echo "Starting QEMU with GDB stub via 'make run'..."
make run DEBUG=1 PLATFORM="$PLATFORM" USE_PCI="$USE_PCI" DRIVE="$DRIVE" PORT="$PORT" &
QEMU_PID=$!
echo $QEMU_PID > "$QEMU_PID_FILE"

echo "QEMU started (PID: $QEMU_PID)"
echo "Waiting for QEMU to initialize..."
sleep 1

# Check if QEMU is still running
if ! kill -0 "$QEMU_PID" 2>/dev/null; then
    echo "ERROR: QEMU exited prematurely"
    exit 1
fi

# Detect which LLDB script to use
if [ -f "script/debug/lldb_crashdump.txt" ]; then
    LLDB_SCRIPT="script/debug/lldb_crashdump.txt"
elif [ -f "script/debug/lldb_simple.txt" ]; then
    LLDB_SCRIPT="script/debug/lldb_simple.txt"
else
    echo "WARNING: No LLDB script found, using basic connection"
    LLDB_SCRIPT=""
fi

# Launch LLDB
echo "Launching LLDB..."
echo "LLDB will connect to localhost:1234"
if [ -n "$LLDB_SCRIPT" ]; then
    echo "Using script: $LLDB_SCRIPT"
    lldb "$KERNEL_ELF" -s "$LLDB_SCRIPT"
else
    # Basic connection without script
    lldb "$KERNEL_ELF" -o "gdb-remote localhost:1234"
fi

# Cleanup happens automatically via trap
echo "LLDB session ended"

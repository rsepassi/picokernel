#!/bin/bash
# Generate code line count table for VMOS project

set -euo pipefail

# Check if cloc is installed
if ! command -v cloc &> /dev/null; then
    echo "Error: cloc is not installed"
    echo "Install with: brew install cloc"
    exit 1
fi

# Function to run cloc and parse results
get_counts() {
    local dir=$1
    local output=$(cloc "$dir" --include-ext=h,c,S --csv 2>/dev/null || echo "")

    # Parse CSV output to extract counts
    # CSV format: files,language,blank,comment,code
    # Language is in field 2, code count is in field 5
    local c_lines=$(echo "$output" | awk -F',' '$2=="C" {print $5}' | head -1)
    local h_lines=$(echo "$output" | awk -F',' '$2=="C/C++ Header" {print $5}' | head -1)
    local s_lines=$(echo "$output" | awk -F',' '$2=="Assembly" {print $5}' | head -1)
    local sum_lines=$(echo "$output" | awk -F',' '$2=="SUM" {print $5}' | head -1)

    # Default to 0 if not found
    c_lines=${c_lines:-0}
    h_lines=${h_lines:-0}
    s_lines=${s_lines:-0}
    sum_lines=${sum_lines:-0}

    echo "$c_lines|$h_lines|$s_lines|$sum_lines"
}

# Format number
format_num() {
    local num=$1
    printf "%'d" "$num"
}

# Print table header
echo ""
echo "| Directory          | C       | Headers | Assembly | Total   |"
echo "|--------------------|---------|---------|----------|---------|"

# Directories to analyze
declare -a dirs=(
    "libc"
    "kernel"
    "driver"
    "platform/shared"
    "platform/arm64"
    "platform/arm32"
    "platform/x64"
    "platform/rv64"
    "platform/rv32"
)

# Process each directory
for dir in "${dirs[@]}"; do
    if [ -d "$dir" ]; then
        counts=$(get_counts "$dir")
        IFS='|' read -r c h s total <<< "$counts"

        # Format numbers
        c_fmt=$(format_num "$c")
        h_fmt=$(format_num "$h")
        s_fmt=$(format_num "$s")
        total_fmt=$(format_num "$total")

        # Print row
        printf "| %-18s | %7s | %7s | %8s | %7s |\n" \
            "$dir/" "$c_fmt" "$h_fmt" "$s_fmt" "$total_fmt"
    fi
done

echo ""

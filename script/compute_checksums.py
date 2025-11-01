#!/usr/bin/env python3
"""
Compute CRC32 checksums for kernel memory sections and patch them into the binary.

This script:
1. Reads the kernel.elf file
2. Extracts .text and .rodata sections
3. Computes CRC32 checksums
4. Patches the checksums into platform_expected_checksums structure
"""

import argparse
import struct
import sys
import subprocess
import zlib
from pathlib import Path


def get_section_data(elf_path, section_name):
    """Extract section data from ELF file using llvm-objcopy."""
    try:
        # Use llvm-objcopy to extract the section
        result = subprocess.run(
            ['llvm-objcopy', '--dump-section', f'{section_name}=/dev/stdout', elf_path],
            capture_output=True,
            check=True
        )
        return result.stdout
    except subprocess.CalledProcessError as e:
        print(f"Error extracting {section_name}: {e}", file=sys.stderr)
        return None


def get_symbol_address(elf_path, symbol_name):
    """Get the address and size of a symbol using llvm-nm."""
    try:
        result = subprocess.run(
            ['llvm-nm', '-S', elf_path],
            capture_output=True,
            check=True,
            text=True
        )

        for line in result.stdout.splitlines():
            if symbol_name in line:
                parts = line.split()
                if len(parts) >= 4 and parts[2] in ('D', 'd', 'B', 'b'):
                    # Format: address size type name
                    addr = int(parts[0], 16)
                    size = int(parts[1], 16) if len(parts[1]) > 0 else 0
                    return addr, size
                elif len(parts) >= 3:
                    # Format without size: address type name
                    addr = int(parts[0], 16)
                    return addr, 0

        print(f"Symbol {symbol_name} not found", file=sys.stderr)
        return None, None
    except subprocess.CalledProcessError as e:
        print(f"Error getting symbol address: {e}", file=sys.stderr)
        return None, None


def patch_binary(elf_path, symbol_name, text_crc, rodata_crc):
    """Patch CRC32 values into the binary at the symbol location."""
    # Get the symbol address
    addr, size = get_symbol_address(elf_path, symbol_name)
    if addr is None:
        print(f"Cannot patch: symbol {symbol_name} not found", file=sys.stderr)
        return False

    # We need to convert virtual address to file offset
    # Use llvm-readelf to get section to segment mapping
    try:
        result = subprocess.run(
            ['llvm-readelf', '-S', elf_path],
            capture_output=True,
            check=True,
            text=True
        )

        # Find the .data or .bss section to get its virtual address and file offset
        # Try .data first, then .bss (checksums might be in either depending on initialization)
        for section_name in ['.data', '.bss']:
            for line in result.stdout.splitlines():
                parts = line.split()
                # Format: [ Nr] Name Type Address Off Size ES Flg Lk Inf Al
                # parts[0] = '[', parts[1] = 'Nr]', parts[2] = 'Name', parts[3] = 'Type', ...
                # We want parts[2] (without brackets) to match section_name
                if len(parts) >= 7:
                    # Remove brackets from section number if present
                    name_col = parts[2] if parts[1].endswith(']') else parts[1]
                    if name_col == section_name:
                        section_type = parts[3] if parts[1].endswith(']') else parts[2]
                        vaddr_col = parts[4] if parts[1].endswith(']') else parts[3]
                        offset_col = parts[5] if parts[1].endswith(']') else parts[4]
                        vaddr = int(vaddr_col, 16)
                        offset = int(offset_col, 16)
                        size_col = parts[6] if parts[1].endswith(']') else parts[5]

                        # Check if symbol is in this section's address range
                        section_size = int(size_col, 16)
                        if vaddr <= addr < vaddr + section_size:
                            # For NOBITS sections (.bss), we need to expand the file
                            if section_type == 'NOBITS':
                                # .bss is not in the file, but we can still patch it
                                # by expanding the file to include this section
                                # Calculate where it would be
                                file_offset = offset + (addr - vaddr)

                                # Read the entire file
                                with open(elf_path, 'rb') as f:
                                    data = bytearray(f.read())

                                # Expand file if needed to include our offset
                                if len(data) < file_offset + 8:
                                    data.extend(b'\x00' * (file_offset + 8 - len(data)))

                                # Patch the two uint32_t values
                                struct.pack_into('<I', data, file_offset, text_crc)
                                struct.pack_into('<I', data, file_offset + 4, rodata_crc)

                                # Write back
                                with open(elf_path, 'wb') as f:
                                    f.write(data)

                                print(f"Patched checksums in {section_name} at offset 0x{file_offset:x}")
                                print(f"  .text CRC32: 0x{text_crc:08x}")
                                print(f"  .rodata CRC32: 0x{rodata_crc:08x}")
                                return True
                            else:
                                # Regular section with file backing
                                file_offset = offset + (addr - vaddr)

                                # Read the entire file
                                with open(elf_path, 'rb') as f:
                                    data = bytearray(f.read())

                                # Patch the two uint32_t values
                                struct.pack_into('<I', data, file_offset, text_crc)
                                struct.pack_into('<I', data, file_offset + 4, rodata_crc)

                                # Write back
                                with open(elf_path, 'wb') as f:
                                    f.write(data)

                                print(f"Patched checksums in {section_name} at offset 0x{file_offset:x}")
                                print(f"  .text CRC32: 0x{text_crc:08x}")
                                print(f"  .rodata CRC32: 0x{rodata_crc:08x}")
                                return True

        print(f"Could not find symbol {symbol_name} in .data or .bss sections", file=sys.stderr)
        return False

    except subprocess.CalledProcessError as e:
        print(f"Error reading ELF sections: {e}", file=sys.stderr)
        return False


def main():
    parser = argparse.ArgumentParser(description='Compute and patch kernel section checksums')
    parser.add_argument('elf_file', help='Path to kernel.elf')
    parser.add_argument('--symbol', default='platform_expected_checksums',
                       help='Symbol name for checksum storage (default: platform_expected_checksums)')
    parser.add_argument('--verify-only', action='store_true',
                       help='Only compute and display checksums, do not patch')

    args = parser.parse_args()

    elf_path = Path(args.elf_file)
    if not elf_path.exists():
        print(f"Error: {elf_path} not found", file=sys.stderr)
        return 1

    # Extract section data
    print(f"Processing {elf_path}")
    text_data = get_section_data(str(elf_path), '.text')
    rodata_data = get_section_data(str(elf_path), '.rodata')

    if text_data is None or rodata_data is None:
        print("Failed to extract sections", file=sys.stderr)
        return 1

    # Compute CRC32 checksums
    text_crc = zlib.crc32(text_data) & 0xFFFFFFFF
    rodata_crc = zlib.crc32(rodata_data) & 0xFFFFFFFF

    print(f".text section: {len(text_data)} bytes, CRC32: 0x{text_crc:08x}")
    print(f".rodata section: {len(rodata_data)} bytes, CRC32: 0x{rodata_crc:08x}")

    if args.verify_only:
        return 0

    # Patch the binary
    if not patch_binary(str(elf_path), args.symbol, text_crc, rodata_crc):
        return 1

    print("Checksums successfully patched into binary")
    return 0


if __name__ == '__main__':
    sys.exit(main())

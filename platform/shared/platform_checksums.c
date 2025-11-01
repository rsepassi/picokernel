// Platform Checksums (shared across all platforms)
// Expected checksums are patched by script/compute_checksums.py during build
// Validates read-only memory sections (.text, .rodata) at boot and after init

#include "kbase.h"
#include "mem_debug.h"

#ifdef KDEBUG

// Expected checksums (populated by build script)
// These are stored in .data (not .rodata) so they don't affect the checksums themselves
// IMPORTANT: Must be initialized to non-zero to prevent placement in .bss
struct {
    uint32_t text_crc32;
    uint32_t rodata_crc32;
} platform_expected_checksums = {
    .text_crc32 = 0xFFFFFFFF,   // Will be patched by build script
    .rodata_crc32 = 0xFFFFFFFF  // Will be patched by build script
};

uint32_t platform_get_expected_text_checksum(void) {
    return platform_expected_checksums.text_crc32;
}

uint32_t platform_get_expected_rodata_checksum(void) {
    return platform_expected_checksums.rodata_crc32;
}

#else

// Stubs for non-debug builds
uint32_t platform_get_expected_text_checksum(void) { return 0; }
uint32_t platform_get_expected_rodata_checksum(void) { return 0; }

#endif

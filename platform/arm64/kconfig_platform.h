// ARM64 Platform Configuration
// Platform-specific configuration for ARM64

#pragma once

#define ARM64_PAGE_SHIFT 16
#define ARM64_PAGE_SIZE (1ULL << ARM64_PAGE_SHIFT) // 64 KB

// MMIO region discovery from FDT
// Maximum number of MMIO regions to discover and map
#define KCONFIG_MAX_MMIO_REGIONS 64

// MMU page table pool sizes (ARM64 64KB granule)
// L2 tables: each covers 4 TB (8192 entries × 512 MB)
#define KCONFIG_ARM64_MAX_L2_TABLES 4
// L3 tables: each covers 512 MB (8192 entries × 64 KB)
#define KCONFIG_ARM64_MAX_L3_TABLES 16

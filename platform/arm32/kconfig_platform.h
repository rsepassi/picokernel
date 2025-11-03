// ARM32 Platform Configuration
// Platform-specific configuration for ARM32

#pragma once

#define ARM32_PAGE_SHIFT 12
#define ARM32_PAGE_SIZE (1UL << ARM32_PAGE_SHIFT) // 4 KB

// MMIO region discovery from FDT
// Maximum number of MMIO regions to discover and map
#define KCONFIG_MAX_MMIO_REGIONS 64

// MMU page table pool sizes (ARM32 4KB pages, short-descriptor format)
// L2 tables: each covers 1 MB (256 entries × 4 KB)
// QEMU -m 128M gives us 128 MB RAM + ~100 MB of MMIO regions = ~230 L2 tables
// Use 384 to provide headroom for PCI MMIO partial mapping
#define KCONFIG_ARM32_MAX_L2_TABLES 384

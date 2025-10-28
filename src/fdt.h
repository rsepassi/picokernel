// Flattened Device Tree (FDT) structures and functions
// Based on the Device Tree Specification

#pragma once

#include <stdint.h>

// FDT magic number
#define FDT_MAGIC 0xd00dfeed

// FDT structure block tokens
#define FDT_BEGIN_NODE  0x00000001
#define FDT_END_NODE    0x00000002
#define FDT_PROP        0x00000003
#define FDT_NOP         0x00000004
#define FDT_END         0x00000009

// FDT header structure (all fields are big-endian)
struct fdt_header {
    uint32_t magic;              // Magic number: 0xd00dfeed
    uint32_t totalsize;          // Total size of FDT in bytes
    uint32_t off_dt_struct;      // Offset to structure block
    uint32_t off_dt_strings;     // Offset to strings block
    uint32_t off_mem_rsvmap;     // Offset to memory reservation block
    uint32_t version;            // FDT version
    uint32_t last_comp_version;  // Last compatible version
    uint32_t boot_cpuid_phys;    // Boot CPU ID
    uint32_t size_dt_strings;    // Size of strings block
    uint32_t size_dt_struct;     // Size of structure block
};

// FDT property structure (in structure block)
struct fdt_prop {
    uint32_t len;      // Length of property value in bytes
    uint32_t nameoff;  // Offset into strings block
};

// FDT memory reservation entry
struct fdt_reserve_entry {
    uint64_t address;
    uint64_t size;
};

// Utility functions for endianness conversion
static inline uint32_t fdt32_to_cpu(uint32_t x) {
    return __builtin_bswap32(x);
}

static inline uint64_t fdt64_to_cpu(uint64_t x) {
    return __builtin_bswap64(x);
}

// VirtIO MMIO device info
typedef struct {
    uint64_t base_addr;
    uint64_t size;
    uint32_t irq;
} virtio_mmio_device_t;

// FDT parsing functions
void fdt_dump(void *fdt);

// Find VirtIO MMIO devices in device tree
// Returns: number of devices found (up to max_devices)
int fdt_find_virtio_mmio(void* fdt, virtio_mmio_device_t* devices, int max_devices);

// PVH Boot Protocol Structures
// Defines structures for PVH (Para-Virtualized Hardware) boot protocol
// Used by QEMU -kernel to boot x64 in a clean, modern way

#pragma once

#include <stdint.h>

// PVH start info magic value
#define HVM_START_MAGIC_VALUE 0x336ec578

// PVH start info structure (from Xen interface)
// Passed in EBX register at boot time
struct hvm_start_info {
  uint32_t magic;          // Magic value: 0x336ec578 "xEn3"
  uint32_t version;        // Version of this structure
  uint32_t flags;          // SIF_xxx flags
  uint32_t nr_modules;     // Number of modules passed to domain
  uint64_t modlist_paddr;  // Physical address of module list
  uint64_t cmdline_paddr;  // Physical address of command line
  uint64_t rsdp_paddr;     // Physical address of RSDP ACPI table
  uint64_t memmap_paddr;   // Physical address of E820 memory map
  uint32_t memmap_entries; // Number of memory map entries
  uint32_t reserved;
} __attribute__((packed));

// E820 memory map entry (pointed to by memmap_paddr)
struct hvm_memmap_table_entry {
  uint64_t addr; // Base address
  uint64_t size; // Length of region
  uint32_t type; // E820 type (see below)
  uint32_t reserved;
} __attribute__((packed));

// E820 memory types
#define E820_RAM 1      // Available RAM
#define E820_RESERVED 2 // Reserved (unavailable)
#define E820_ACPI 3     // ACPI reclaimable
#define E820_NVS 4      // ACPI NVS
#define E820_UNUSABLE 5 // Unusable memory
#define E820_PMEM 7     // Persistent memory (NVDIMM)

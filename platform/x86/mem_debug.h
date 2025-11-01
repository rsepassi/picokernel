// x64 Memory Debugging Utilities
// Tools for validating and debugging memory layout

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Memory region definitions
#define MEM_REGION_PAGE_TABLES_BASE 0x100000
#define MEM_REGION_PAGE_TABLES_SIZE 0x5000
#define MEM_REGION_KERNEL_BASE 0x200000
#define MEM_REGION_KERNEL_TEXT_END 0x20D000
#define MEM_REGION_RODATA_BASE 0x20D000
#define MEM_REGION_RODATA_END 0x20F000
#define MEM_REGION_BSS_BASE 0x20F000
#define MEM_REGION_KERNEL_END 0x23DC00

// Memory region descriptor
typedef struct {
  const char *name;
  uint64_t base;
  uint64_t size;
  bool writable;
} mem_region_t;

// Dump memory region to console (hex dump)
// addr: starting address
// len: number of bytes to dump
void mem_dump(const void *addr, uint64_t len);

// Dump a specific memory region by address range
void mem_dump_range(const char *label, uint64_t start, uint64_t end);

// Validate that a memory region contains expected pattern
// Returns true if valid, false if corrupted
bool mem_validate_pattern(const void *addr, uint64_t len, uint8_t pattern);

// Check if an address range overlaps with another
bool mem_ranges_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start,
                        uint64_t b_end);

// Validate critical memory regions (page tables, kernel sections)
// Returns true if all checks pass, false otherwise
bool mem_validate_critical_regions(void);

// Dump page table contents
void mem_dump_page_tables(void);

// Validate a specific memory region
bool mem_validate_region(const mem_region_t *region);

// Print memory map summary
void mem_print_map(void);

// Add a memory guard/canary value at a specific address
void mem_set_guard(void *addr, uint64_t pattern);

// Check if a memory guard is intact
bool mem_check_guard(const void *addr, uint64_t pattern);

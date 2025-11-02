// x64 Platform Memory Management
// Implements memory discovery via PVH boot protocol and E820 memory map
// Builds free region list after accounting for reserved areas

#include "platform.h"
#include "platform_mem.h"
#include "printk.h"
#include "pvh.h"
#include <stddef.h>
#include <stdint.h>

// Linker symbols for kernel boundaries
extern uint8_t _start[];
extern uint8_t _end[];
extern uint8_t stack_bottom[];
extern uint8_t stack_top[];

// Page table location (set up by boot.S)
#define PAGE_TABLES_BASE 0x100000ULL
#define PAGE_TABLES_SIZE 0x5000ULL // 20 KiB (5 pages: PML4, PDPT, 2x PD, PT)

// Helper: Get E820 type name
static const char *e820_type_name(uint32_t type) {
  switch (type) {
  case E820_RAM:
    return "Available RAM";
  case E820_RESERVED:
    return "Reserved";
  case E820_ACPI:
    return "ACPI Reclaimable";
  case E820_NVS:
    return "ACPI NVS";
  case E820_UNUSABLE:
    return "Unusable";
  case E820_PMEM:
    return "Persistent Memory";
  default:
    return "Unknown";
  }
}

// Validate PVH start info structure
static int validate_pvh_info(struct hvm_start_info *pvh_info) {
  if (!pvh_info) {
    printk("[MEM] ERROR: PVH start info pointer is NULL\n");
    return -1;
  }

  // Check magic number
  if (pvh_info->magic != HVM_START_MAGIC_VALUE) {
    printk("[MEM] ERROR: Invalid PVH magic: 0x");
    printk_hex32(pvh_info->magic);
    printk(" (expected 0x336ec578)\n");
    return -1;
  }

  // Check memory map
  if (pvh_info->memmap_paddr == 0 || pvh_info->memmap_entries == 0) {
    printk("[MEM] ERROR: PVH memory map not provided\n");
    printk("  memmap_paddr: 0x");
    printk_hex64(pvh_info->memmap_paddr);
    printk("\n  memmap_entries: ");
    printk_dec(pvh_info->memmap_entries);
    printk("\n");
    return -1;
  }

  return 0;
}

// Parse PVH E820 memory map and extract RAM regions
static int parse_e820_map(struct hvm_start_info *pvh_info,
                          mem_region_t *ram_regions, int *num_ram_regions,
                          int max_regions) {
  struct hvm_memmap_table_entry *memmap =
      (struct hvm_memmap_table_entry *)(uintptr_t)pvh_info->memmap_paddr;

  printk("\n[MEM] === PVH E820 Memory Map ===\n");
  printk("[MEM] Found ");
  printk_dec(pvh_info->memmap_entries);
  printk(" memory map entries:\n\n");

  *num_ram_regions = 0;

  for (uint32_t i = 0; i < pvh_info->memmap_entries; i++) {
    uint64_t base = memmap[i].addr;
    uint64_t size = memmap[i].size;
    uint32_t type = memmap[i].type;

    // Print entry
    printk("[MEM]   [");
    printk_dec(i);
    printk("] 0x");
    printk_hex64(base);
    printk(" - 0x");
    printk_hex64(base + size - 1);
    printk("  (");
    printk_dec(size >> 20); // Size in MiB
    printk(" MiB)  Type ");
    printk_dec(type);
    printk(": ");
    printk(e820_type_name(type));
    printk("\n");

    // Collect available RAM regions
    if (type == E820_RAM) {
      if (*num_ram_regions >= max_regions) {
        printk("[MEM] WARNING: Too many RAM regions, skipping some\n");
        continue;
      }

      ram_regions[*num_ram_regions].base = base;
      ram_regions[*num_ram_regions].size = size;
      (*num_ram_regions)++;
    }
  }

  printk("\n[MEM] Discovered ");
  printk_dec(*num_ram_regions);
  printk(" RAM region(s)\n");

  return 0;
}

// Print reserved regions for debugging
static void print_reserved_regions(uintptr_t kernel_start, uintptr_t kernel_end,
                                    uintptr_t stack_start, uintptr_t stack_end,
                                    uintptr_t pt_start, uintptr_t pt_end,
                                    struct hvm_start_info *pvh_info) {
  printk("\n[MEM] === Reserved Regions ===\n");

  printk("[MEM]   Page Tables: 0x");
  printk_hex64(pt_start);
  printk(" - 0x");
  printk_hex64(pt_end);
  printk("  (");
  printk_dec(pt_end - pt_start);
  printk(" bytes)\n");

  printk("[MEM]   Kernel:      0x");
  printk_hex64(kernel_start);
  printk(" - 0x");
  printk_hex64(kernel_end);
  printk("  (");
  printk_dec(kernel_end - kernel_start);
  printk(" bytes)\n");

  printk("[MEM]   Stack:       0x");
  printk_hex64(stack_start);
  printk(" - 0x");
  printk_hex64(stack_end);
  printk("  (");
  printk_dec(stack_end - stack_start);
  printk(" bytes)\n");

  printk("[MEM]   PVH Info:    0x");
  printk_hex64((uintptr_t)pvh_info);
  printk(" - 0x");
  printk_hex64((uintptr_t)pvh_info + sizeof(struct hvm_start_info));
  printk("  (");
  printk_dec(sizeof(struct hvm_start_info));
  printk(" bytes)\n");

  if (pvh_info->memmap_paddr) {
    uintptr_t memmap_start = pvh_info->memmap_paddr;
    size_t memmap_size =
        pvh_info->memmap_entries * sizeof(struct hvm_memmap_table_entry);
    printk("[MEM]   E820 Map:    0x");
    printk_hex64(memmap_start);
    printk(" - 0x");
    printk_hex64(memmap_start + memmap_size);
    printk("  (");
    printk_dec(memmap_size);
    printk(" bytes)\n");
  }
}

// Subtract a reserved region from available regions (may split regions)
// Returns: number of output regions
static int subtract_reserved_region(const mem_region_t *input_regions,
                                     int num_input, uintptr_t reserved_base,
                                     uintptr_t reserved_end,
                                     mem_region_t *output_regions,
                                     int max_output) {
  int num_output = 0;

  for (int i = 0; i < num_input; i++) {
    uintptr_t region_base = input_regions[i].base;
    uintptr_t region_end = input_regions[i].base + input_regions[i].size;

    // Case 1: No overlap, keep entire region
    if (region_end <= reserved_base || region_base >= reserved_end) {
      if (num_output >= max_output) {
        printk("[MEM] ERROR: Too many output regions during subtraction\n");
        return num_output;
      }
      output_regions[num_output++] = input_regions[i];
      continue;
    }

    // Case 2: Reserved region completely covers this region, skip it
    if (reserved_base <= region_base && reserved_end >= region_end) {
      continue; // Entire region is reserved
    }

    // Case 3: Reserved region is in the middle, split into two regions
    if (reserved_base > region_base && reserved_end < region_end) {
      // Add region before reserved area
      if (num_output >= max_output) {
        printk("[MEM] ERROR: Too many output regions during split (before)\n");
        return num_output;
      }
      output_regions[num_output].base = region_base;
      output_regions[num_output].size = reserved_base - region_base;
      num_output++;

      // Add region after reserved area
      if (num_output >= max_output) {
        printk("[MEM] ERROR: Too many output regions during split (after)\n");
        return num_output;
      }
      output_regions[num_output].base = reserved_end;
      output_regions[num_output].size = region_end - reserved_end;
      num_output++;
      continue;
    }

    // Case 4: Reserved region overlaps start of region
    if (reserved_base <= region_base && reserved_end < region_end) {
      if (num_output >= max_output) {
        printk("[MEM] ERROR: Too many output regions (start overlap)\n");
        return num_output;
      }
      output_regions[num_output].base = reserved_end;
      output_regions[num_output].size = region_end - reserved_end;
      num_output++;
      continue;
    }

    // Case 5: Reserved region overlaps end of region
    if (reserved_base > region_base && reserved_end >= region_end) {
      if (num_output >= max_output) {
        printk("[MEM] ERROR: Too many output regions (end overlap)\n");
        return num_output;
      }
      output_regions[num_output].base = region_base;
      output_regions[num_output].size = reserved_base - region_base;
      num_output++;
      continue;
    }
  }

  return num_output;
}

// Build free region list by subtracting all reserved areas from RAM regions
static int build_free_regions(const mem_region_t *ram_regions,
                               int num_ram_regions, mem_region_t *free_regions,
                               int max_free_regions,
                               struct hvm_start_info *pvh_info) {
  // Get reserved region boundaries
  uintptr_t kernel_start = (uintptr_t)_start;
  uintptr_t kernel_end = (uintptr_t)_end;
  uintptr_t stack_start = (uintptr_t)stack_bottom;
  uintptr_t stack_end = (uintptr_t)stack_top;
  uintptr_t pt_start = PAGE_TABLES_BASE;
  uintptr_t pt_end = PAGE_TABLES_BASE + PAGE_TABLES_SIZE;

  // Print reserved regions for debugging
  print_reserved_regions(kernel_start, kernel_end, stack_start, stack_end,
                          pt_start, pt_end, pvh_info);

  // Use double buffering to avoid overwriting input during subtraction
  mem_region_t temp_regions_a[KCONFIG_MAX_MEM_REGIONS];
  mem_region_t temp_regions_b[KCONFIG_MAX_MEM_REGIONS];

  // Copy initial RAM regions to temp buffer
  int num_regions = num_ram_regions;
  for (int i = 0; i < num_ram_regions; i++) {
    temp_regions_a[i] = ram_regions[i];
  }

  // Subtract page tables
  num_regions = subtract_reserved_region(temp_regions_a, num_regions, pt_start,
                                          pt_end, temp_regions_b, max_free_regions);

  // Subtract kernel
  num_regions =
      subtract_reserved_region(temp_regions_b, num_regions, kernel_start,
                               kernel_end, temp_regions_a, max_free_regions);

  // Subtract stack
  num_regions =
      subtract_reserved_region(temp_regions_a, num_regions, stack_start,
                               stack_end, temp_regions_b, max_free_regions);

  // Subtract PVH start info structure
  uintptr_t pvh_start = (uintptr_t)pvh_info;
  uintptr_t pvh_end = pvh_start + sizeof(struct hvm_start_info);
  num_regions = subtract_reserved_region(temp_regions_b, num_regions, pvh_start,
                                          pvh_end, temp_regions_a, max_free_regions);

  // Subtract E820 memory map
  if (pvh_info->memmap_paddr) {
    uintptr_t memmap_start = pvh_info->memmap_paddr;
    size_t memmap_size =
        pvh_info->memmap_entries * sizeof(struct hvm_memmap_table_entry);
    uintptr_t memmap_end = memmap_start + memmap_size;
    num_regions =
        subtract_reserved_region(temp_regions_a, num_regions, memmap_start,
                                 memmap_end, temp_regions_b, max_free_regions);
  } else {
    // If no memmap to subtract, copy from temp_regions_a to temp_regions_b
    for (int i = 0; i < num_regions; i++) {
      temp_regions_b[i] = temp_regions_a[i];
    }
  }

  // Copy final results to output
  for (int i = 0; i < num_regions && i < max_free_regions; i++) {
    free_regions[i] = temp_regions_b[i];
  }

  return num_regions;
}

// Initialize memory management from PVH boot info
int platform_mem_init(platform_t *platform, void *pvh_info_ptr) {
  struct hvm_start_info *pvh_info = (struct hvm_start_info *)pvh_info_ptr;

  printk("\n[MEM] === x64 Memory Discovery (PVH Boot) ===\n");

  // Validate PVH start info
  if (validate_pvh_info(pvh_info) < 0) {
    return -1;
  }

  // Save PVH info pointer
  platform->pvh_info = pvh_info;

  printk("[MEM] PVH start info valid (magic: 0x");
  printk_hex32(pvh_info->magic);
  printk(", version: ");
  printk_dec(pvh_info->version);
  printk(")\n");

  // Parse E820 memory map to get RAM regions
  mem_region_t ram_regions[KCONFIG_MAX_MEM_REGIONS];
  int num_ram_regions = 0;
  if (parse_e820_map(pvh_info, ram_regions, &num_ram_regions,
                     KCONFIG_MAX_MEM_REGIONS) < 0) {
    return -1;
  }

  if (num_ram_regions == 0) {
    printk("[MEM] ERROR: No RAM regions found in E820 map\n");
    return -1;
  }

  // Build free region list by subtracting reserved areas
  platform->num_mem_regions =
      build_free_regions(ram_regions, num_ram_regions, platform->mem_regions,
                         KCONFIG_MAX_MEM_REGIONS, pvh_info);

  // Print final free regions
  printk("\n[MEM] === Free Memory Regions ===\n");
  printk("[MEM] Found ");
  printk_dec(platform->num_mem_regions);
  printk(" free region(s):\n");

  uint64_t total_free = 0;
  for (int i = 0; i < platform->num_mem_regions; i++) {
    printk("[MEM]   [");
    printk_dec(i);
    printk("] 0x");
    printk_hex64(platform->mem_regions[i].base);
    printk(" - 0x");
    printk_hex64(platform->mem_regions[i].base + platform->mem_regions[i].size);
    printk("  (");
    printk_dec(platform->mem_regions[i].size >> 20); // Size in MiB
    printk(" MiB)\n");

    total_free += platform->mem_regions[i].size;
  }

  printk("[MEM] Total free memory: ");
  printk_dec(total_free >> 20);
  printk(" MiB\n");

  return 0;
}

// API: Get memory regions for kernel allocators
mem_region_list_t platform_mem_regions(platform_t *platform) {
  mem_region_list_t list;
  list.regions = platform->mem_regions;
  list.count = platform->num_mem_regions;
  return list;
}

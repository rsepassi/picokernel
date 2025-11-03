// x64 Boot Context Parsing
// Parse PVH start info and E820 memory map, populate platform_t

#include "kconfig.h"
#include "platform.h"
#include "platform_impl.h"
#include "platform_mem.h"
#include "printk.h"
#include "pvh.h"
#include <stddef.h>
#include <stdint.h>

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
    printk("[BOOT] ERROR: PVH start info pointer is NULL\n");
    return -1;
  }

  // Check magic number
  if (pvh_info->magic != HVM_START_MAGIC_VALUE) {
    printk("[BOOT] ERROR: Invalid PVH magic: 0x");
    printk_hex32(pvh_info->magic);
    printk(" (expected 0x336ec578)\n");
    return -1;
  }

  // Check memory map
  if (pvh_info->memmap_paddr == 0 || pvh_info->memmap_entries == 0) {
    printk("[BOOT] ERROR: PVH memory map not provided\n");
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
// Populates platform->mem_regions[] and platform->num_mem_regions
static int parse_e820_map(struct hvm_start_info *pvh_info,
                          platform_t *platform) {
  struct hvm_memmap_table_entry *memmap =
      (struct hvm_memmap_table_entry *)(uintptr_t)pvh_info->memmap_paddr;

  printk("\n[BOOT] === PVH E820 Memory Map ===\n");
  printk("[BOOT] Found ");
  printk_dec(pvh_info->memmap_entries);
  printk(" memory map entries:\n\n");

  platform->num_mem_regions = 0;

  for (uint32_t i = 0; i < pvh_info->memmap_entries; i++) {
    uint64_t base = memmap[i].addr;
    uint64_t size = memmap[i].size;
    uint32_t type = memmap[i].type;

    // Print entry
    printk("[BOOT]   [");
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
      if (platform->num_mem_regions >= KCONFIG_MAX_MEM_REGIONS) {
        printk("[BOOT] WARNING: Too many RAM regions, skipping some\n");
        continue;
      }

      platform->mem_regions[platform->num_mem_regions].base = base;
      platform->mem_regions[platform->num_mem_regions].size = size;
      platform->num_mem_regions++;
    }
  }

  printk("\n[BOOT] Discovered ");
  printk_dec(platform->num_mem_regions);
  printk(" RAM region(s)\n");

  return 0;
}

// Parse PVH boot context and populate platform with memory regions
// boot_context is actually a PVH start info pointer on x64
// Returns: 0 on success, negative on error
int platform_boot_context_parse(platform_t *platform, void *boot_context) {
  struct hvm_start_info *pvh_info = (struct hvm_start_info *)boot_context;

  printk("\n[BOOT] === x64 Boot Context Parsing (PVH) ===\n");

  // Validate PVH start info
  if (validate_pvh_info(pvh_info) < 0) {
    return -1;
  }

  // Save PVH info pointer
  platform->pvh_info = pvh_info;

  printk("[BOOT] PVH start info valid (magic: 0x");
  printk_hex32(pvh_info->magic);
  printk(", version: ");
  printk_dec(pvh_info->version);
  printk(")\n");
  printk("[BOOT] PVH rsdp_paddr: 0x");
  printk_hex64(pvh_info->rsdp_paddr);
  printk("\n");

  // Parse E820 memory map and populate platform->mem_regions[]
  if (parse_e820_map(pvh_info, platform) < 0) {
    return -1;
  }

  if (platform->num_mem_regions == 0) {
    printk("[BOOT] ERROR: No RAM regions found in E820 map\n");
    return -1;
  }

  return 0;
}

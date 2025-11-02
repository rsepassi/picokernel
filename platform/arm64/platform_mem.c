// ARM64 Platform Memory Management
// MMU setup and free memory region list building

#include "kbase.h"
#include "platform.h"
#include "printk.h"
#include <stddef.h>
#include <stdint.h>

// Forward declarations
void platform_mem_init(platform_t *platform, void *fdt);
void platform_mem_debug_mmu(void);

// Linker-provided symbols
extern uint8_t _start[];
extern uint8_t _end[];
extern uint8_t stack_bottom[];
extern uint8_t stack_top[];

// Page table entry flags for ARM64 with 64KB granule
#define PTE_VALID (1UL << 0)    // Valid entry
#define PTE_TABLE (1UL << 1)    // Table descriptor (upper levels)
#define PTE_PAGE (1UL << 1)     // Page descriptor (level 3)
#define PTE_AF (1UL << 10)      // Access flag
#define PTE_NORMAL (0UL << 2)   // MAIR index 0 (Normal memory)
#define PTE_DEVICE (1UL << 2)   // MAIR index 1 (Device memory)
#define PTE_INNER_SH (3UL << 8) // Inner shareable
#define PTE_OUTER_SH (2UL << 8) // Outer shareable
#define PTE_NS (1UL << 5)       // Non-secure

// Page table descriptors
#define PTE_L1_TABLE (PTE_VALID | PTE_TABLE)
#define PTE_L2_TABLE (PTE_VALID | PTE_TABLE)
#define PTE_L3_PAGE_NORMAL                                                     \
  (PTE_VALID | PTE_PAGE | PTE_AF | PTE_NORMAL | PTE_INNER_SH)
#define PTE_L3_PAGE_DEVICE                                                     \
  (PTE_VALID | PTE_PAGE | PTE_AF | PTE_DEVICE | PTE_OUTER_SH)

// ARM64 with 64KB granule, 48-bit address space:
// - L1 table: 8192 entries, each covers 512 GB
// - L2 table: 8192 entries, each covers 64 MB
// - L3 table: 8192 entries, each covers 64 KB

// Allocate page tables statically in BSS (aligned to 64KB)
static uint64_t page_table_l1[8192] __attribute__((aligned(65536)));
static uint64_t page_table_l2_ram[8192] __attribute__((aligned(65536)));
static uint64_t page_table_l2_mmio[8192] __attribute__((aligned(65536)));
static uint64_t page_table_l3_ram[8][8192]
    __attribute__((aligned(65536))); // 8 L3 tables for RAM
static uint64_t page_table_l3_mmio[2][8192]
    __attribute__((aligned(65536))); // 2 L3 tables for MMIO

// Helper: Check if two memory ranges overlap
static int ranges_overlap(uintptr_t base1, size_t size1, uintptr_t base2,
                          size_t size2) {
  uintptr_t end1 = base1 + size1;
  uintptr_t end2 = base2 + size2;
  return !(end1 <= base2 || end2 <= base1);
}

// Helper: Subtract reserved region from available regions
// May split a region into two parts if reserved region is in the middle
static void subtract_reserved_region(mem_region_t *regions, int *count,
                                     uintptr_t reserved_base,
                                     size_t reserved_size) {
  uintptr_t reserved_end = reserved_base + reserved_size;

  for (int i = 0; i < *count; i++) {
    uintptr_t region_base = regions[i].base;
    uintptr_t region_end = region_base + regions[i].size;

    // Skip if no overlap
    if (!ranges_overlap(region_base, regions[i].size, reserved_base,
                        reserved_size)) {
      continue;
    }

    // Case 1: Reserved region completely contains this region
    if (reserved_base <= region_base && reserved_end >= region_end) {
      // Remove this region entirely
      for (int j = i; j < *count - 1; j++) {
        regions[j] = regions[j + 1];
      }
      (*count)--;
      i--; // Re-check this index
      continue;
    }

    // Case 2: Reserved region at the start
    if (reserved_base <= region_base && reserved_end < region_end) {
      regions[i].base = reserved_end;
      regions[i].size = region_end - reserved_end;
      continue;
    }

    // Case 3: Reserved region at the end
    if (reserved_base > region_base && reserved_end >= region_end) {
      regions[i].size = reserved_base - region_base;
      continue;
    }

    // Case 4: Reserved region in the middle - split into two regions
    if (reserved_base > region_base && reserved_end < region_end) {
      // First part: [region_base, reserved_base)
      regions[i].size = reserved_base - region_base;

      // Second part: [reserved_end, region_end) - add as new region
      if (*count < KCONFIG_MAX_MEM_REGIONS) {
        regions[*count].base = reserved_end;
        regions[*count].size = region_end - reserved_end;
        (*count)++;
      }
    }
  }
}

// Setup MMU with identity mapping
static void setup_mmu(platform_t *platform) {
  printk("Setting up ARM64 MMU (64KB pages, 48-bit address space)...\n");

  // Clear all page tables
  for (int i = 0; i < 8192; i++) {
    page_table_l1[i] = 0;
    page_table_l2_ram[i] = 0;
    page_table_l2_mmio[i] = 0;
  }
  for (int t = 0; t < 8; t++) {
    for (int i = 0; i < 8192; i++) {
      page_table_l3_ram[t][i] = 0;
    }
  }
  for (int t = 0; t < 2; t++) {
    for (int i = 0; i < 8192; i++) {
      page_table_l3_mmio[t][i] = 0;
    }
  }

  // L1 table setup (covers entire address space)
  // Entry 0: 0x00000000-0x7FFFFFFFFF (first 512 GB) - points to L2 MMIO
  // Entry for RAM region (0x40000000 is in first 512 GB)
  page_table_l1[0] = ((uint64_t)page_table_l2_mmio) | PTE_L1_TABLE;

  // L2 MMIO table: map low MMIO regions and RAM
  // Each L2 entry covers 64 MB
  // 0x00000000-0x0FFFFFFF: MMIO regions (GIC, UART, VirtIO)
  // 0x40000000-0x4FFFFFFF: RAM region

  // Map MMIO region (0x08000000-0x0FFFFFFF, 128 MB) using L3 tables
  // L2 index = address / 64MB
  // 0x08000000 / 0x4000000 = 2
  // 0x0C000000 / 0x4000000 = 3
  page_table_l2_mmio[2] =
      ((uint64_t)page_table_l3_mmio[0]) | PTE_L2_TABLE; // 0x08000000-0x0BFFFFFF
  page_table_l2_mmio[3] =
      ((uint64_t)page_table_l3_mmio[1]) | PTE_L2_TABLE; // 0x0C000000-0x0FFFFFFF

  // Map RAM region (0x40000000-0x47FFFFFF, 128 MB) using L3 tables
  // 0x40000000 / 0x4000000 = 16
  // 0x44000000 / 0x4000000 = 17
  for (int i = 0; i < 8 && (16 + i) < 8192; i++) {
    page_table_l2_mmio[16 + i] =
        ((uint64_t)page_table_l3_ram[i]) | PTE_L2_TABLE;
  }

  // L3 MMIO tables: Map MMIO devices (64KB pages)
  // GIC: 0x08000000-0x08020000 (128 KB)
  // UART: 0x09000000-0x09001000 (4 KB)
  // VirtIO MMIO: 0x0A000000-0x0A200000 (2 MB)

  // Map 0x08000000-0x0BFFFFFF (64 MB) as device memory
  for (int i = 0; i < 1024; i++) { // 64 MB / 64 KB = 1024 pages
    uint64_t page_addr = 0x08000000ULL + (i * 0x10000ULL);
    page_table_l3_mmio[0][i] = page_addr | PTE_L3_PAGE_DEVICE;
  }

  // Map 0x0C000000-0x0FFFFFFF (64 MB) as device memory
  for (int i = 0; i < 1024; i++) {
    uint64_t page_addr = 0x0C000000ULL + (i * 0x10000ULL);
    page_table_l3_mmio[1][i] = page_addr | PTE_L3_PAGE_DEVICE;
  }

  // L3 RAM tables: Map discovered RAM regions as normal memory
  // Map all discovered RAM regions
  for (int r = 0; r < platform->num_mem_regions; r++) {
    uintptr_t ram_base = platform->mem_regions[r].base;
    size_t ram_size = platform->mem_regions[r].size;

    printk("  Mapping RAM region ");
    printk_dec(r);
    printk(": 0x");
    printk_hex64(ram_base);
    printk(" - 0x");
    printk_hex64(ram_base + ram_size);
    printk(" (");
    printk_dec(ram_size / 1024 / 1024);
    printk(" MB)\n");

    // Map RAM as 64KB pages
    uintptr_t addr = ram_base;
    while (addr < ram_base + ram_size) {
      // Calculate L2 and L3 indices
      uint32_t l2_idx = (addr >> 26) & 0x1FFF; // Bits [38:26]
      uint32_t l3_idx = (addr >> 16) & 0x1FFF; // Bits [25:16]

      // Determine which L3 table to use (0-7 for RAM region starting at
      // 0x40000000)
      int l3_table_num = l2_idx - 16; // RAM starts at L2 index 16

      if (l3_table_num >= 0 && l3_table_num < 8) {
        page_table_l3_ram[l3_table_num][l3_idx] = addr | PTE_L3_PAGE_NORMAL;
      }

      addr += 0x10000; // Next 64KB page
    }
  }

  // Configure MAIR_EL1 (Memory Attribute Indirection Register)
  // Index 0: Normal memory (Inner/Outer Write-Back, Read/Write-Allocate,
  // Non-transient) Index 1: Device memory (Device-nGnRnE: non-Gathering,
  // non-Reordering, no Early Write Acknowledgement)
  uint64_t mair = 0x00000000000044ffULL;
  __asm__ volatile("msr mair_el1, %0" : : "r"(mair));

  // Configure TCR_EL1 (Translation Control Register)
  // For 64KB granule, 48-bit address space:
  // - TG0 = 01 (64KB granule for TTBR0_EL1)
  // - T0SZ = 16 (64 - 16 = 48 bit address space)
  // - IRGN0 = 01 (Inner Write-Back Read-Allocate Write-Allocate Cacheable)
  // - ORGN0 = 01 (Outer Write-Back Read-Allocate Write-Allocate Cacheable)
  // - SH0 = 11 (Inner Shareable)
  // - IPS = 000 (32-bit physical address space, but we'll use higher for
  // safety)
  uint64_t tcr = (1ULL << 14) | // TG0 = 01 (64KB)
                 (16ULL << 0) | // T0SZ = 16 (48-bit VA)
                 (1ULL << 8) |  // IRGN0 = 01
                 (1ULL << 10) | // ORGN0 = 01
                 (3ULL << 12) | // SH0 = 11
                 (5ULL << 32);  // IPS = 101 (48-bit PA)
  __asm__ volatile("msr tcr_el1, %0" : : "r"(tcr));

  // Set TTBR0_EL1 (Translation Table Base Register)
  uint64_t ttbr0 = (uint64_t)page_table_l1;
  __asm__ volatile("msr ttbr0_el1, %0" : : "r"(ttbr0));

  // Ensure all writes are complete
  __asm__ volatile("dsb sy" ::: "memory");
  __asm__ volatile("isb" ::: "memory");

  // Enable MMU via SCTLR_EL1
  uint64_t sctlr;
  __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
  sctlr |= (1 << 0);  // M: Enable MMU
  sctlr |= (1 << 2);  // C: Enable data cache
  sctlr |= (1 << 12); // I: Enable instruction cache
  __asm__ volatile("msr sctlr_el1, %0" : : "r"(sctlr));
  __asm__ volatile("isb" ::: "memory");

  printk("MMU enabled with caching\n");
}

// Build free memory region list
// initial_num_regions: number of discovered RAM regions (before subtraction)
static void build_free_regions(platform_t *platform, int initial_num_regions) {
  printk("\nBuilding free memory region list...\n");

  // Print initial discovered RAM regions
  for (int i = 0; i < initial_num_regions; i++) {
    printk("  Initial region ");
    printk_dec(i);
    printk(": 0x");
    printk_hex64(platform->mem_regions[i].base);
    printk(" - 0x");
    printk_hex64(platform->mem_regions[i].base + platform->mem_regions[i].size);
    printk(" (");
    printk_dec(platform->mem_regions[i].size / 1024 / 1024);
    printk(" MB)\n");
  }

  // Reserve DTB region
  if (platform->fdt_base != 0 && platform->fdt_size != 0) {
    printk("  Reserving DTB: 0x");
    printk_hex64(platform->fdt_base);
    printk(" - 0x");
    printk_hex64(platform->fdt_base + platform->fdt_size);
    printk(" (");
    printk_dec(platform->fdt_size / 1024);
    printk(" KB)\n");
    subtract_reserved_region(platform->mem_regions, &platform->num_mem_regions,
                             platform->fdt_base, platform->fdt_size);
  }

  // Reserve kernel region (_start to _end)
  uintptr_t kernel_base = (uintptr_t)_start;
  uintptr_t kernel_end = (uintptr_t)_end;
  size_t kernel_size = kernel_end - kernel_base;
  printk("  Reserving kernel: 0x");
  printk_hex64(kernel_base);
  printk(" - 0x");
  printk_hex64(kernel_end);
  printk(" (");
  printk_dec(kernel_size / 1024);
  printk(" KB)\n");
  subtract_reserved_region(platform->mem_regions, &platform->num_mem_regions,
                           kernel_base, kernel_size);

  // Reserve stack region
  uintptr_t stack_base = (uintptr_t)stack_bottom;
  uintptr_t stack_top_addr = (uintptr_t)stack_top;
  size_t stack_size = stack_top_addr - stack_base;
  printk("  Reserving stack: 0x");
  printk_hex64(stack_base);
  printk(" - 0x");
  printk_hex64(stack_top_addr);
  printk(" (");
  printk_dec(stack_size / 1024);
  printk(" KB)\n");
  subtract_reserved_region(platform->mem_regions, &platform->num_mem_regions,
                           stack_base, stack_size);

  // Reserve page table region
  uintptr_t pt_base = (uintptr_t)page_table_l1;
  size_t pt_size = sizeof(page_table_l1) + sizeof(page_table_l2_ram) +
                   sizeof(page_table_l2_mmio) + sizeof(page_table_l3_ram) +
                   sizeof(page_table_l3_mmio);
  printk("  Reserving page tables: 0x");
  printk_hex64(pt_base);
  printk(" - 0x");
  printk_hex64(pt_base + pt_size);
  printk(" (");
  printk_dec(pt_size / 1024);
  printk(" KB)\n");
  subtract_reserved_region(platform->mem_regions, &platform->num_mem_regions,
                           pt_base, pt_size);

  // Print final free regions
  printk("\nFree memory regions:\n");
  size_t total_free = 0;
  for (int i = 0; i < platform->num_mem_regions; i++) {
    printk("  Region ");
    printk_dec(i);
    printk(": 0x");
    printk_hex64(platform->mem_regions[i].base);
    printk(" - 0x");
    printk_hex64(platform->mem_regions[i].base + platform->mem_regions[i].size);
    printk(" (");
    printk_dec(platform->mem_regions[i].size / 1024 / 1024);
    printk(" MB)\n");
    total_free += platform->mem_regions[i].size;
  }
  printk("Total free memory: ");
  printk_dec(total_free / 1024 / 1024);
  printk(" MB\n\n");
}

// Initialize memory management and MMU
void platform_mem_init(platform_t *platform, void *fdt) {
  printk("=== ARM64 Memory Management Initialization ===\n");

  printk("FDT pointer: 0x");
  printk_hex64((uint64_t)fdt);
  printk("\n");

  // Get FDT base and size
  platform->fdt_base = (uintptr_t)fdt;
  if (fdt) {
    printk("Reading FDT header...\n");
    struct fdt_header *header = (struct fdt_header *)fdt;
    platform->fdt_size = kbe32toh(header->totalsize);

    printk("FDT size: ");
    printk_dec(platform->fdt_size);
    printk(" bytes\n");

    // Align size up to 64KB
    platform->fdt_size = (platform->fdt_size + 0xFFFF) & ~0xFFFF;
  } else {
    printk("WARNING: FDT pointer is NULL\n");
    platform->fdt_size = 0;
  }

  platform->kernel_end = (uintptr_t)_end;

  // Check if MMU is already enabled
  uint64_t sctlr;
  __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
  printk("Current SCTLR_EL1: 0x");
  printk_hex64(sctlr);
  printk(" (MMU ");
  printk(sctlr & 1 ? "enabled" : "disabled");
  printk(")\n");

  printk("Parsing FDT...\n");
  // Parse FDT to discover all memory regions and device addresses
  if (platform_boot_context_parse(platform, fdt) != 0) {
    printk("ERROR: Failed to parse boot context\n");
    return;
  }

  // Save initial region count before subtraction
  int initial_num_regions = platform->num_mem_regions;

  // Setup MMU with identity mapping
  setup_mmu(platform);

  // Build free memory region list (subtracts reserved regions in-place)
  build_free_regions(platform, initial_num_regions);

  // Debug: Verify MMU configuration
  platform_mem_debug_mmu();

  printk("=== Memory Management Initialization Complete ===\n\n");
}

// Get list of available memory regions
mem_region_list_t platform_mem_regions(platform_t *platform) {
  mem_region_list_t list;
  list.regions = platform->mem_regions;
  list.count = platform->num_mem_regions;
  return list;
}

// Debug: Verify MMU configuration
void platform_mem_debug_mmu(void) {
  printk("\n=== ARM64 MMU Configuration Debug ===\n");

  // Read SCTLR_EL1
  uint64_t sctlr;
  __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
  printk("SCTLR_EL1: 0x");
  printk_hex64(sctlr);
  printk("\n");
  printk("  MMU enabled (M): ");
  printk(sctlr & (1 << 0) ? "Yes" : "No");
  printk("\n");
  printk("  Data cache (C): ");
  printk(sctlr & (1 << 2) ? "Enabled" : "Disabled");
  printk("\n");
  printk("  Instruction cache (I): ");
  printk(sctlr & (1 << 12) ? "Enabled" : "Disabled");
  printk("\n");

  // Read MAIR_EL1
  uint64_t mair;
  __asm__ volatile("mrs %0, mair_el1" : "=r"(mair));
  printk("MAIR_EL1: 0x");
  printk_hex64(mair);
  printk("\n");

  // Read TCR_EL1
  uint64_t tcr;
  __asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr));
  printk("TCR_EL1: 0x");
  printk_hex64(tcr);
  printk("\n");
  printk("  T0SZ: ");
  printk_dec(tcr & 0x3F);
  printk(" (");
  printk_dec(64 - (tcr & 0x3F));
  printk("-bit address space)\n");
  printk("  TG0: ");
  printk_dec((tcr >> 14) & 0x3);
  printk(" (");
  switch ((tcr >> 14) & 0x3) {
  case 0:
    printk("4KB");
    break;
  case 1:
    printk("64KB");
    break;
  case 2:
    printk("16KB");
    break;
  default:
    printk("Reserved");
    break;
  }
  printk(" granule)\n");

  // Read TTBR0_EL1
  uint64_t ttbr0;
  __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
  printk("TTBR0_EL1: 0x");
  printk_hex64(ttbr0);
  printk(" (page table base)\n");

  printk("=== End MMU Debug ===\n\n");
}

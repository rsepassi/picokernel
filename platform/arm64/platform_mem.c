// ARM64 Platform Memory Management
// MMU setup and free memory region list building

#include "kbase.h"
#include "platform.h"
#include "printk.h"
#include <stddef.h>
#include <stdint.h>

// Forward declarations
void platform_mem_init(platform_t *platform);
void platform_mem_debug_mmu(void);
void platform_uart_init(platform_t *platform);

// Linker-provided symbols
extern uint8_t _start[];
extern uint8_t _end[];
extern uint8_t stack_bottom[];
extern uint8_t stack_top[];

// ARM64 MMU address breakdown (64KB granule, 48-bit VA)
// Bits [15:0]:  Page offset (64 KB)
// Bits [28:16]: L3 table index (13 bits, 8192 entries)
// Bits [41:29]: L2 table index (13 bits, 8192 entries)
// Bits [47:42]: L1 table index (6 bits, 64 entries)
#define ARM64_L3_SHIFT 16
#define ARM64_L2_SHIFT 29
#define ARM64_L1_SHIFT 42

#define ARM64_L3_COVERAGE (1ULL << ARM64_L2_SHIFT) // 512 MB (8192 * 64 KB)
#define ARM64_L2_COVERAGE (1ULL << ARM64_L1_SHIFT) // 4 TB (8192 * 512 MB)
#define ARM64_L1_COVERAGE (1ULL << 48)             // 256 TB (64 * 4 TB)

#define ARM64_L1_INDEX(addr) (((addr) >> ARM64_L1_SHIFT) & 0x3F) // Bits [47:42]
#define ARM64_L2_INDEX(addr)                                                   \
  (((addr) >> ARM64_L2_SHIFT) & 0x1FFF) // Bits [41:29]
#define ARM64_L3_INDEX(addr)                                                   \
  (((addr) >> ARM64_L3_SHIFT) & 0x1FFF) // Bits [28:16]

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
// - L1 table: 64 entries, each covers ARM64_L2_COVERAGE (4 TB)
// - L2 table: 8192 entries, each covers ARM64_L3_COVERAGE (512 MB)
// - L3 table: 8192 entries, each covers ARM64_PAGE_SIZE (64 KB)

// Page tables are now allocated in platform_t structure
// Pool sizes defined in kconfig_platform.h

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

// Helper: Allocate an L2 table from the pool
static uint64_t *get_l2_table(platform_t *platform) {
  KASSERT(platform->next_l2_table < KCONFIG_ARM64_MAX_L2_TABLES,
          "L2 table pool exhausted");
  return platform->page_table_l2_pool[platform->next_l2_table++];
}

// Helper: Allocate an L3 table from the pool
static uint64_t *get_l3_table(platform_t *platform) {
  KASSERT(platform->next_l3_table < KCONFIG_ARM64_MAX_L3_TABLES,
          "L3 table pool exhausted");
  return platform->page_table_l3_pool[platform->next_l3_table++];
}

// Helper: Map a single 64KB page with identity mapping
static void map_page(platform_t *platform, uint64_t phys_addr, uint64_t flags) {
  uint32_t l1_idx = ARM64_L1_INDEX(phys_addr);
  uint32_t l2_idx = ARM64_L2_INDEX(phys_addr);
  uint32_t l3_idx = ARM64_L3_INDEX(phys_addr);

  // Ensure L1 → L2 mapping exists
  if (!(platform->page_table_l1[l1_idx] & PTE_VALID)) {
    uint64_t *l2_table = get_l2_table(platform);
    platform->page_table_l1[l1_idx] = ((uint64_t)l2_table) | PTE_L1_TABLE;
  }

  // Get L2 table and ensure L2 → L3 mapping exists
  uint64_t *l2_table =
      (uint64_t *)(platform->page_table_l1[l1_idx] & ~0xFFFFULL);
  if (!(l2_table[l2_idx] & PTE_VALID)) {
    uint64_t *l3_table = get_l3_table(platform);
    l2_table[l2_idx] = ((uint64_t)l3_table) | PTE_L2_TABLE;
  }

  // Map the page in L3 table
  uint64_t *l3_table = (uint64_t *)(l2_table[l2_idx] & ~0xFFFFULL);
  l3_table[l3_idx] = phys_addr | flags;
}

// Helper: Map a range of pages with the same flags
static void map_page_range(platform_t *platform, uint64_t base, uint64_t size,
                           uint64_t flags) {
  for (uint64_t addr = base; addr < base + size; addr += ARM64_PAGE_SIZE) {
    map_page(platform, addr, flags);
  }
}

// Setup MMU with identity mapping
// NOTE: noinline to prevent load hoisting from platform after
// platform_boot_context_parse
__attribute__((noinline)) static void setup_mmu(platform_t *platform) {
  KLOG("Setting up ARM64 MMU (64KB pages, 48-bit address space)...");

  // Reset allocation counters
  platform->next_l2_table = 0;
  platform->next_l3_table = 0;

  // Clear all page tables
  for (int i = 0; i < 64; i++) {
    platform->page_table_l1[i] = 0;
  }
  for (int t = 0; t < KCONFIG_ARM64_MAX_L2_TABLES; t++) {
    for (int i = 0; i < 8192; i++) {
      platform->page_table_l2_pool[t][i] = 0;
    }
  }
  for (int t = 0; t < KCONFIG_ARM64_MAX_L3_TABLES; t++) {
    for (int i = 0; i < 8192; i++) {
      platform->page_table_l3_pool[t][i] = 0;
    }
  }

  // Map MMIO region (0x08000000-0x0FFFFFFF, 128 MB)
  // Note: These MMIO ranges are hardcoded for the QEMU virt machine layout.
  // While the exact device addresses are discovered from FDT (and used by
  // drivers), we map a broad MMIO region here to ensure all devices are
  // accessible before device discovery completes. This is a common pattern in
  // embedded systems. Typical devices in this range:
  //   GIC: 0x08000000-0x08020000 (128 KB)
  //   UART: 0x09000000-0x09001000 (4 KB)
  //   VirtIO MMIO: 0x0A000000-0x0A200000 (2 MB)
  KLOG("Mapping MMIO region: 0x08000000 - 0x0FFFFFFF (128 MB)");
  map_page_range(platform, 0x08000000ULL, 0x08000000ULL, PTE_L3_PAGE_DEVICE);

  // Map all discovered RAM regions
  for (int r = 0; r < platform->num_mem_regions; r++) {
    uint64_t ram_base = platform->mem_regions[r].base;
    uint64_t ram_size = platform->mem_regions[r].size;

    KLOG("Mapping RAM region %d: 0x%llx - 0x%llx (%llu MB)", r,
         (unsigned long long)ram_base,
         (unsigned long long)(ram_base + ram_size),
         (unsigned long long)(ram_size / 1024 / 1024));

    map_page_range(platform, ram_base, ram_size, PTE_L3_PAGE_NORMAL);
  }

  // Map PCI ECAM region if discovered from FDT
  if (platform->pci_ecam_base != 0 && platform->pci_ecam_size != 0) {
    KLOG(
        "Mapping PCI ECAM: 0x%llx - 0x%llx (%llu MB)",
        (unsigned long long)platform->pci_ecam_base,
        (unsigned long long)(platform->pci_ecam_base + platform->pci_ecam_size),
        (unsigned long long)(platform->pci_ecam_size / 1024 / 1024));

    map_page_range(platform, platform->pci_ecam_base, platform->pci_ecam_size,
                   PTE_L3_PAGE_DEVICE);
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
  uint64_t ttbr0 = (uint64_t)platform->page_table_l1;
  __asm__ volatile("msr ttbr0_el1, %0" : : "r"(ttbr0));

  // Flush writes
  __asm__ volatile("dsb sy" ::: "memory");

  // Invalidate TLB (Translation Lookaside Buffer) - ensure clean state
  __asm__ volatile("tlbi vmalle1" ::: "memory");
  __asm__ volatile("dsb sy" ::: "memory");

  // Invalidate entire instruction cache to PoU (Point of Unification)
  // Ensures no stale instructions are cached with wrong translations
  __asm__ volatile("ic iallu" ::: "memory");
  __asm__ volatile("dsb sy" ::: "memory");
  __asm__ volatile("isb" ::: "memory");

  KLOG("x");

  // Check if MMU is already enabled
  uint64_t sctlr;
  __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
  KASSERT((sctlr & 1) == 0, "MMU expected to be disabled");

  KLOG("x");
  // First enable just the MMU
  sctlr |= (1 << 0); // M: Enable MMU
  __asm__ volatile("msr sctlr_el1, %0" : : "r"(sctlr));
  KLOG("x");
  __asm__ volatile("isb" ::: "memory");

  // Now enable caches
  sctlr |= (1 << 2);  // C: Enable data cache
  sctlr |= (1 << 12); // I: Enable instruction cache
  __asm__ volatile("msr sctlr_el1, %0" : : "r"(sctlr));
  __asm__ volatile("isb" ::: "memory");

  printk("MMU enabled with caching\n");
}

// Build free memory region list
// NOTE: noinline to prevent load hoisting from platform after
// platform_boot_context_parse
__attribute__((noinline)) static void build_free_regions(platform_t *platform) {
  printk("\nBuilding free memory region list...\n");
  int initial_num_regions = platform->num_mem_regions;

  // Print initial discovered RAM regions
  for (int i = 0; i < initial_num_regions; i++) {
    KLOG("Initial region %d: 0x%llx - 0x%llx (%llu MB)", i,
         (unsigned long long)platform->mem_regions[i].base,
         (unsigned long long)(platform->mem_regions[i].base +
                              platform->mem_regions[i].size),
         (unsigned long long)(platform->mem_regions[i].size / 1024 / 1024));
  }

  // Reserve DTB region
  if (platform->fdt_base != 0 && platform->fdt_size != 0) {
    KLOG("Reserving DTB: 0x%llx - 0x%llx (%llu KB)",
         (unsigned long long)platform->fdt_base,
         (unsigned long long)(platform->fdt_base + platform->fdt_size),
         (unsigned long long)(platform->fdt_size / 1024));
    subtract_reserved_region(platform->mem_regions, &platform->num_mem_regions,
                             platform->fdt_base, platform->fdt_size);
  }

  // Reserve kernel region (_start to _end)
  // This includes .text, .rodata, .data, and .bss sections
  // Note: Stack and page tables are in .bss, so they're already included
  uintptr_t kernel_base = (uintptr_t)_start;
  uintptr_t kernel_end = (uintptr_t)_end;
  size_t kernel_size = kernel_end - kernel_base;
  KLOG("Reserving kernel: 0x%llx - 0x%llx (%llu KB, includes stack and page "
       "tables in .bss)",
       (unsigned long long)kernel_base, (unsigned long long)kernel_end,
       (unsigned long long)(kernel_size / 1024));
  subtract_reserved_region(platform->mem_regions, &platform->num_mem_regions,
                           kernel_base, kernel_size);

  // Print final free regions
  printk("\nFree memory regions:\n");
  size_t total_free = 0;
  for (int i = 0; i < platform->num_mem_regions; i++) {
    KLOG("Region %d: 0x%llx - 0x%llx (%llu MB)", i,
         (unsigned long long)platform->mem_regions[i].base,
         (unsigned long long)(platform->mem_regions[i].base +
                              platform->mem_regions[i].size),
         (unsigned long long)(platform->mem_regions[i].size / 1024 / 1024));
    total_free += platform->mem_regions[i].size;
  }
  KLOG("Total free memory: %llu MB\n",
       (unsigned long long)(total_free / 1024 / 1024));
}

// Initialize memory management and MMU
void platform_mem_init(platform_t *platform) {
  setup_mmu(platform);
  build_free_regions(platform);
  KDEBUG_VALIDATE(platform_mem_debug_mmu());
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

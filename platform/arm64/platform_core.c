// ARM64 Platform Core Initialization
// Combines: FDT parsing, MMU setup, and initialization orchestration

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kbase.h"
#include "kconfig.h"
#include "mem_debug.h"
#include "platform.h"
#include "printk.h"

#include "interrupt.h"
#include "timer.h"
#include "virtio/virtio_rng.h"

#include "libfdt/libfdt.h"

// =============================================================================
// Forward Declarations
// =============================================================================

extern void pci_scan_devices(platform_t *platform);
extern void mmio_scan_devices(platform_t *platform);
void platform_mem_debug_mmu(void);
void platform_uart_init(platform_t *platform);
void platform_boot_context_parse(platform_t *platform, void *boot_context);
void platform_mem_init(platform_t *platform);

// Linker-provided symbols
extern uint8_t _start[];
extern uint8_t _end[];
extern uint8_t stack_bottom[];
extern uint8_t stack_top[];

// =============================================================================
// ARM64 MMU Configuration Constants (64KB granule, 48-bit VA)
// =============================================================================

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

// Index masks for page table levels
#define ARM64_L1_MASK 0x3FULL   // 6 bits for L1 index (64 entries)
#define ARM64_L2_MASK 0x1FFFULL // 13 bits for L2 index (8192 entries)
#define ARM64_L3_MASK 0x1FFFULL // 13 bits for L3 index (8192 entries)

// Page alignment mask (64KB granule)
#define ARM64_PAGE_ALIGN_MASK 0xFFFFULL // Lower 16 bits for 64KB pages

#define ARM64_L1_INDEX(addr) (((addr) >> ARM64_L1_SHIFT) & ARM64_L1_MASK)
#define ARM64_L2_INDEX(addr) (((addr) >> ARM64_L2_SHIFT) & ARM64_L2_MASK)
#define ARM64_L3_INDEX(addr) (((addr) >> ARM64_L3_SHIFT) & ARM64_L3_MASK)

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

// =============================================================================
// FDT Parsing (from platform_boot_context.c)
// =============================================================================

// Helper: Check if compatible property matches any in list
static int compatible_match(const void *fdt, int node,
                            const char **compat_list) {
  int len;
  const char *compat = fdt_getprop(fdt, node, "compatible", &len);
  if (!compat || len <= 0)
    return 0;

  // Compatible property can contain multiple null-terminated strings
  const char *end = compat + len;
  while (compat < end) {
    for (int i = 0; compat_list[i] != NULL; i++) {
      if (strcmp(compat, compat_list[i]) == 0) {
        return 1;
      }
    }
    // Move to next string in compatible list
    compat += strlen(compat) + 1;
  }
  return 0;
}

// Parse FDT boot context and populate platform with memory regions
// Asserts on error (does not return error codes)
void platform_boot_context_parse(platform_t *platform, void *boot_context) {
  void *fdt = boot_context;

  KDEBUG_LOG("ARM64 FDT parsing");
  KASSERT(fdt, "FDT is NULL");

  // Validate FDT header
  int err = fdt_check_header(fdt);
  KASSERT(err == 0, "Invalid FDT header");

  // Store FDT location and size
  platform->fdt_base = (uintptr_t)fdt;
  platform->fdt_size = fdt_totalsize(fdt);
  KDEBUG_LOG("FDT size: %u bytes", platform->fdt_size);
  // Align size up to 64KB page boundary
  platform->fdt_size = KALIGN(platform->fdt_size, ARM64_PAGE_SIZE);

  // Initialize platform memory regions
  platform->num_mem_regions = 0;

  // Initialize MMIO regions
  platform->num_mmio_regions = 0;

  // Initialize device addresses to zero
  platform->gic_dist_base = 0;
  platform->gic_cpu_base = 0;
  platform->pci_ecam_base = 0;
  platform->pci_ecam_size = 0;
  platform->pci_mmio_base = 0;
  platform->pci_mmio_size = 0;
  platform->uart_base = 0;

  KDEBUG_LOG("Traversing device tree");

  // Compatible string lists for device matching
  const char *uart_compat[] = {"arm,pl011", NULL};
  const char *gic_compat[] = {"arm,gic-400", "arm,cortex-a15-gic",
                              "arm,cortex-a9-gic", "arm,gic-v2", NULL};

  // Single traversal through the device tree
  int node = -1;
  int depth = 0;
  while ((node = fdt_next_node(fdt, node, &depth)) >= 0) {
    // Get node name
    const char *name = fdt_get_name(fdt, node, NULL);
    if (!name)
      continue;

    // Check for memory nodes (memory@<addr>)
    if (memcmp(name, "memory@", 7) == 0 || strcmp(name, "memory") == 0) {
      if (platform->num_mem_regions < KCONFIG_MAX_MEM_REGIONS) {
        int len;
        const void *reg = fdt_getprop(fdt, node, "reg", &len);
        if (reg && len >= 16) {
          kregion_t *region = &platform->mem_regions[platform->num_mem_regions];
          region->base = kload_be64((const uint8_t *)reg);
          region->size = kload_be64((const uint8_t *)reg + 8);

          // Link into doubly-linked list
          region->next = NULL;
          if (platform->num_mem_regions == 0) {
            // First region - no previous
            region->prev = NULL;
          } else {
            // Link to previous region
            kregion_t *prev_region =
                &platform->mem_regions[platform->num_mem_regions - 1];
            prev_region->next = region;
            region->prev = prev_region;
          }

          platform->num_mem_regions++;
        }
      }
      continue;
    }

    // Check for UART
    if (platform->uart_base == 0 && compatible_match(fdt, node, uart_compat)) {
      int len;
      const void *reg = fdt_getprop(fdt, node, "reg", &len);
      if (reg && len >= 16) {
        platform->uart_base = kload_be64((const uint8_t *)reg);
      }
      continue;
    }

    // Check for GIC
    if (platform->gic_dist_base == 0 &&
        compatible_match(fdt, node, gic_compat)) {
      int len;
      const void *reg = fdt_getprop(fdt, node, "reg", &len);
      if (reg && len >= 16) {
        platform->gic_dist_base = kload_be64((const uint8_t *)reg);
        // GIC CPU interface is second register pair
        if (len >= 32) {
          platform->gic_cpu_base = kload_be64((const uint8_t *)reg + 16);
        }
      }
      continue;
    }

    // Check for PCI ECAM
    if (platform->pci_ecam_base == 0) {
      static const char *pci_compat[] = {"pci-host-ecam-generic", NULL};
      if (compatible_match(fdt, node, pci_compat)) {
        // Found PCI node - get ECAM base
        int len;
        const void *reg = fdt_getprop(fdt, node, "reg", &len);
        if (reg && len >= 16) {
          platform->pci_ecam_base = kload_be64((const uint8_t *)reg);
          platform->pci_ecam_size = kload_be64((const uint8_t *)reg + 8);
        }

        // Parse PCI ranges property to get MMIO region for BAR allocation
        const uint8_t *ranges = fdt_getprop(fdt, node, "ranges", &len);
        if (ranges && len >= 28) {
          // PCI ranges format: (child-addr, parent-addr, size) tuples
          // Each tuple is 7 cells (28 bytes):
          //   3 cells (12 bytes): child address (flags + addr)
          //   2 cells (8 bytes):  parent address
          //   2 cells (8 bytes):  size
          // We want 64-bit MMIO space (flags = 0x03000000 or 0x02000000)
          for (int offset = 0; offset + 28 <= len; offset += 28) {
            const uint8_t *entry = ranges + offset;

            // Read child address flags (first 4 bytes)
            uint32_t flags = kload_be32(entry);

            // Read parent address (bytes 12-19)
            uint64_t parent_addr = kload_be64(entry + 12);

            // Read size (bytes 20-27)
            uint64_t size = kload_be64(entry + 20);

            // Check if this is 64-bit MMIO space (0x03000000 =
            // prefetchable, 0x02000000 = 32-bit MMIO, 0x43000000 = 64-bit
            // non-prefetchable)
            uint32_t space_code = flags & 0x03000000;
            if (space_code == 0x03000000 || space_code == 0x02000000) {
              // Found MMIO space - use this for BAR allocation
              platform->pci_mmio_base = parent_addr;
              platform->pci_mmio_size = size;
              break; // Use the first MMIO range we find
            }
          }
        }
      }
    }

    // Generic MMIO region collection for all device nodes with reg property
    // Only collect from nodes at depth 2 (direct children of root, since root
    // is at depth 1)
    if (depth == 2 && platform->num_mmio_regions < KCONFIG_MAX_MMIO_REGIONS) {
      // Skip memory nodes (already handled separately)
      if (memcmp(name, "memory@", 7) == 0 || strcmp(name, "memory") == 0) {
        continue;
      }

      // Only collect nodes that represent actual devices (have both "reg" and
      // "compatible" properties). This filters out virtual/container nodes like
      // aliases, chosen, cpus, etc. which lack physical device properties.
      int reg_len, compat_len;
      const void *reg = fdt_getprop(fdt, node, "reg", &reg_len);
      const void *compat = fdt_getprop(fdt, node, "compatible", &compat_len);

      if (reg && compat && reg_len >= 16) {
        uint64_t base = kload_be64((const uint8_t *)reg);
        uint64_t size = kload_be64((const uint8_t *)reg + 8);

        // Align size up to ARM64_PAGE_SIZE (64KB)
        size = KALIGN(size, ARM64_PAGE_SIZE);

        // Store in MMIO regions array
        platform->mmio_regions[platform->num_mmio_regions].base = base;
        platform->mmio_regions[platform->num_mmio_regions].size = size;
        platform->num_mmio_regions++;
      }
    }
  }

  KDEBUG_LOG("FDT parse complete");

  // Print discovered information
  KLOG("Discovered from FDT:");
  KLOG("  RAM regions: %d", platform->num_mem_regions);
  for (int i = 0; i < platform->num_mem_regions; i++) {
    uint64_t base = platform->mem_regions[i].base;
    uint64_t size = platform->mem_regions[i].size;
    KLOG("    Region %d: 0x%llx - 0x%llx (%llu MB)", i,
         (unsigned long long)base, (unsigned long long)(base + size),
         (unsigned long long)(size / 1024 / 1024));
  }

  if (platform->uart_base != 0) {
    KLOG("  UART: 0x%llx", (unsigned long long)platform->uart_base);
  }

  if (platform->gic_dist_base != 0) {
    KLOG("  GIC Distributor: 0x%llx",
         (unsigned long long)platform->gic_dist_base);
  }

  if (platform->gic_cpu_base != 0) {
    KLOG("  GIC CPU Interface: 0x%llx",
         (unsigned long long)platform->gic_cpu_base);
  }

  if (platform->pci_ecam_base != 0) {
    KLOG("  PCI ECAM: 0x%llx (size: 0x%llx)",
         (unsigned long long)platform->pci_ecam_base,
         (unsigned long long)platform->pci_ecam_size);
  }

  if (platform->pci_mmio_base != 0) {
    KLOG("  PCI MMIO: 0x%llx (size: 0x%llx)",
         (unsigned long long)platform->pci_mmio_base,
         (unsigned long long)platform->pci_mmio_size);
  }

  // Initialize BAR allocator for bare-metal PCI device configuration
  // Use FDT-discovered MMIO range if available, otherwise default to 0x10000000
  // (256MB physical address - safe region above typical low memory)
  platform->pci_next_bar_addr =
      platform->pci_mmio_base ? platform->pci_mmio_base : 0x10000000;

  KLOG("  MMIO regions: %d", platform->num_mmio_regions);
  KDEBUG_VALIDATE({
    for (int i = 0; i < platform->num_mmio_regions; i++) {
      uint64_t base = platform->mmio_regions[i].base;
      uint64_t size = platform->mmio_regions[i].size;
      KLOG("    Region %d: 0x%llx - 0x%llx (size: 0x%llx)", i,
           (unsigned long long)base, (unsigned long long)(base + size),
           (unsigned long long)size);
    }
  });
}

// =============================================================================
// MMU Setup and Memory Region Management (from platform_mem.c)
// =============================================================================

// Helper: Check if two memory ranges overlap
static int ranges_overlap(uintptr_t base1, size_t size1, uintptr_t base2,
                          size_t size2) {
  uintptr_t end1 = base1 + size1;
  uintptr_t end2 = base2 + size2;
  return !(end1 <= base2 || end2 <= base1);
}

// Helper: Subtract reserved region from available regions (linked list version)
// May split a region into two parts if reserved region is in the middle
// regions: array of regions (for allocation)
// head: pointer to head pointer of linked list
// tail: pointer to tail pointer of linked list
// count: pointer to count of regions
static void subtract_reserved_region(kregion_t *regions, kregion_t **head,
                                     kregion_t **tail, int *count,
                                     uintptr_t reserved_base,
                                     size_t reserved_size) {
  uintptr_t reserved_end = reserved_base + reserved_size;

  kregion_t *region = *head;
  while (region != NULL) {
    kregion_t *next = region->next; // Save next before we might modify it
    uintptr_t region_base = region->base;
    uintptr_t region_end = region_base + region->size;

    // Skip if no overlap
    if (!ranges_overlap(region_base, region->size, reserved_base,
                        reserved_size)) {
      region = next;
      continue;
    }

    // Case 1: Reserved region completely contains this region
    if (reserved_base <= region_base && reserved_end >= region_end) {
      // Remove this region from the linked list
      if (region->prev != NULL) {
        region->prev->next = region->next;
      } else {
        *head = region->next; // Removing head
      }
      if (region->next != NULL) {
        region->next->prev = region->prev;
      } else {
        *tail = region->prev; // Removing tail
      }
      (*count)--;
      region = next;
      continue;
    }

    // Case 2: Reserved region at the start
    if (reserved_base <= region_base && reserved_end < region_end) {
      region->base = reserved_end;
      region->size = region_end - reserved_end;
      region = next;
      continue;
    }

    // Case 3: Reserved region at the end
    if (reserved_base > region_base && reserved_end >= region_end) {
      region->size = reserved_base - region_base;
      region = next;
      continue;
    }

    // Case 4: Reserved region in the middle - split into two regions
    if (reserved_base > region_base && reserved_end < region_end) {
      // First part: [region_base, reserved_base)
      region->size = reserved_base - region_base;

      // Second part: [reserved_end, region_end) - add as new region
      if (*count < KCONFIG_MAX_MEM_REGIONS) {
        kregion_t *new_region = &regions[*count];
        new_region->base = reserved_end;
        new_region->size = region_end - reserved_end;
        new_region->prev = region;
        new_region->next = region->next;

        // Insert after current region
        if (region->next != NULL) {
          region->next->prev = new_region;
        } else {
          *tail = new_region; // New region is now the tail
        }
        region->next = new_region;

        (*count)++;
      }
    }

    region = next;
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
  // Physical address must be 64KB aligned for ARM64 64KB pages
  KASSERT((phys_addr & ARM64_PAGE_ALIGN_MASK) == 0,
          "Physical address must be 64KB aligned for mapping");

  uint32_t l1_idx = ARM64_L1_INDEX(phys_addr);
  uint32_t l2_idx = ARM64_L2_INDEX(phys_addr);
  uint32_t l3_idx = ARM64_L3_INDEX(phys_addr);

  // Ensure L1 → L2 mapping exists
  if (!(platform->page_table_l1[l1_idx] & PTE_VALID)) {
    uint64_t *l2_table = get_l2_table(platform);
    uint64_t l2_addr = (uint64_t)l2_table;
    KASSERT((l2_addr & ARM64_PAGE_ALIGN_MASK) == 0,
            "L2 table must be 64KB aligned");
    platform->page_table_l1[l1_idx] = l2_addr | PTE_L1_TABLE;
  }

  // Get L2 table and ensure L2 → L3 mapping exists
  uint64_t *l2_table =
      (uint64_t *)(platform->page_table_l1[l1_idx] & ~ARM64_PAGE_ALIGN_MASK);
  if (!(l2_table[l2_idx] & PTE_VALID)) {
    uint64_t *l3_table = get_l3_table(platform);
    uint64_t l3_addr = (uint64_t)l3_table;
    KASSERT((l3_addr & ARM64_PAGE_ALIGN_MASK) == 0,
            "L3 table must be 64KB aligned");
    l2_table[l2_idx] = l3_addr | PTE_L2_TABLE;
  }

  // Map the page in L3 table
  uint64_t *l3_table = (uint64_t *)(l2_table[l2_idx] & ~ARM64_PAGE_ALIGN_MASK);

  // Skip if page is already mapped (handles overlapping regions from FDT)
  if (l3_table[l3_idx] & PTE_VALID) {
    // If already mapped, verify flags are the same
    uint64_t existing_entry = l3_table[l3_idx];
    uint64_t new_entry = phys_addr | flags;
    KASSERT(existing_entry == new_entry, "Conflicting mapping flags");
    return;
  }

  l3_table[l3_idx] = phys_addr | flags;
}

// Helper: Map a range of pages with the same flags
static void map_page_range(platform_t *platform, uint64_t base, uint64_t size,
                           uint64_t flags) {
  // Align base down to page boundary, and extend size to cover the range
  uint64_t base_aligned = KALIGN_BACK(base, ARM64_PAGE_SIZE);
  uint64_t end = base + size;
  uint64_t end_aligned = KALIGN(end, ARM64_PAGE_SIZE);
  uint64_t size_aligned = end_aligned - base_aligned;

  for (uint64_t addr = base_aligned; addr < base_aligned + size_aligned;
       addr += ARM64_PAGE_SIZE) {
    map_page(platform, addr, flags);
  }
}

// Setup MMU with identity mapping
// NOTE: noinline to prevent load hoisting from platform after
// platform_boot_context_parse
__attribute__((noinline)) static void setup_mmu(platform_t *platform) {
  KDEBUG_LOG("Setting up ARM64 MMU (64KB pages, 48-bit address space)...");

  // Print kernel location for debugging
  KDEBUG_LOG("Kernel location: _start=0x%llx, _end=0x%llx, size=%llu KB",
             (unsigned long long)(uintptr_t)_start,
             (unsigned long long)(uintptr_t)_end,
             (unsigned long long)((uintptr_t)_end - (uintptr_t)_start) / 1024);

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

  // Map MMIO regions discovered from FDT
  // All device MMIO regions are discovered during FDT parsing and stored in
  // platform->mmio_regions[]. We map each region individually with device
  // memory attributes. Region sizes are aligned to ARM64_PAGE_SIZE (64KB).
  KDEBUG_LOG("Mapping %d MMIO regions from FDT:", platform->num_mmio_regions);
  for (int r = 0; r < platform->num_mmio_regions; r++) {
    uint64_t mmio_base = platform->mmio_regions[r].base;
    uint64_t mmio_size = platform->mmio_regions[r].size;

    KDEBUG_LOG("  Region %d: 0x%llx - 0x%llx (0x%llx bytes)", r,
               (unsigned long long)mmio_base,
               (unsigned long long)(mmio_base + mmio_size),
               (unsigned long long)mmio_size);

    map_page_range(platform, mmio_base, mmio_size, PTE_L3_PAGE_DEVICE);
  }

  // Map UART explicitly (it's parsed from FDT but not in MMIO regions list)
  if (platform->uart_base != 0) {
    KDEBUG_LOG("Mapping UART: 0x%llx", (unsigned long long)platform->uart_base);
    map_page_range(platform, platform->uart_base, 0x1000, PTE_L3_PAGE_DEVICE);
  }

  // Map GIC explicitly (parsed from FDT but not in MMIO regions list)
  if (platform->gic_dist_base != 0) {
    KDEBUG_LOG("Mapping GIC Distributor: 0x%llx",
               (unsigned long long)platform->gic_dist_base);
    map_page_range(platform, platform->gic_dist_base, 0x10000,
                   PTE_L3_PAGE_DEVICE);
  }
  if (platform->gic_cpu_base != 0) {
    KDEBUG_LOG("Mapping GIC CPU Interface: 0x%llx",
               (unsigned long long)platform->gic_cpu_base);
    map_page_range(platform, platform->gic_cpu_base, 0x10000,
                   PTE_L3_PAGE_DEVICE);
  }

  // Map PCI MMIO region explicitly (for device BARs)
  // Note: This is separate from PCI ECAM (configuration space)
  if (platform->pci_mmio_base != 0) {
    KDEBUG_LOG("Mapping PCI MMIO: 0x%llx (size: 0x%llx)",
               (unsigned long long)platform->pci_mmio_base,
               (unsigned long long)platform->pci_mmio_size);
    map_page_range(platform, platform->pci_mmio_base, platform->pci_mmio_size,
                   PTE_L3_PAGE_DEVICE);
  }

  // Map all discovered RAM regions
  for (int r = 0; r < platform->num_mem_regions; r++) {
    uint64_t ram_base = platform->mem_regions[r].base;
    uint64_t ram_size = platform->mem_regions[r].size;

    KDEBUG_LOG("Mapping RAM region %d: 0x%llx - 0x%llx (%llu MB)", r,
               (unsigned long long)ram_base,
               (unsigned long long)(ram_base + ram_size),
               (unsigned long long)(ram_size / 1024 / 1024));

    map_page_range(platform, ram_base, ram_size, PTE_L3_PAGE_NORMAL);
  }

  // Note: PCI ECAM is now mapped via the MMIO regions loop above (discovered
  // from FDT and included in platform->mmio_regions[])

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

  KDEBUG_LOG("MMU setup: caches invalidated, barriers complete");

  // Check if MMU is already enabled
  uint64_t sctlr;
  __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
  KASSERT((sctlr & 1) == 0, "MMU expected to be disabled");

  KDEBUG_LOG("MMU setup: enabling MMU...");
  // First enable just the MMU
  sctlr |= (1 << 0); // M: Enable MMU
  __asm__ volatile("msr sctlr_el1, %0" : : "r"(sctlr));
  KDEBUG_LOG("MMU setup: MMU enabled, applying barrier");
  __asm__ volatile("isb" ::: "memory");

  // Now enable caches
  sctlr |= (1 << 2);  // C: Enable data cache
  sctlr |= (1 << 12); // I: Enable instruction cache
  __asm__ volatile("msr sctlr_el1, %0" : : "r"(sctlr));
  __asm__ volatile("isb" ::: "memory");

  KDEBUG_LOG("MMU enabled with caching");
}

// Build free memory region list
// NOTE: noinline to prevent load hoisting from platform after
// platform_boot_context_parse
__attribute__((noinline)) static void build_free_regions(platform_t *platform) {
  KDEBUG_LOG("Building free memory region list...");

  // Head and tail of linked list (starts at first region if any exist)
  kregion_t *head =
      (platform->num_mem_regions > 0) ? &platform->mem_regions[0] : NULL;
  kregion_t *tail = NULL;

  // Find tail (last region in initial list)
  if (head != NULL) {
    tail = head;
    while (tail->next != NULL) {
      tail = tail->next;
    }
  }

  // Print initial discovered RAM regions
  kregion_t *region = head;
  int idx = 0;
  while (region != NULL) {
    KDEBUG_LOG("Initial region %d: 0x%llx - 0x%llx (%llu MB)", idx,
               (unsigned long long)region->base,
               (unsigned long long)(region->base + region->size),
               (unsigned long long)(region->size / 1024 / 1024));
    region = region->next;
    idx++;
  }

  // Reserve DTB region
  if (platform->fdt_base != 0 && platform->fdt_size != 0) {
    KDEBUG_LOG("Reserving DTB: 0x%llx - 0x%llx (%llu KB)",
               (unsigned long long)platform->fdt_base,
               (unsigned long long)(platform->fdt_base + platform->fdt_size),
               (unsigned long long)(platform->fdt_size / 1024));
    subtract_reserved_region(platform->mem_regions, &head, &tail,
                             &platform->num_mem_regions, platform->fdt_base,
                             platform->fdt_size);
  }

  // Reserve kernel region (_start to _end)
  // This includes .text, .rodata, .data, and .bss sections
  // Note: Stack and page tables are in .bss, so they're already included
  uintptr_t kernel_base = (uintptr_t)_start;
  uintptr_t kernel_end = (uintptr_t)_end;
  size_t kernel_size = kernel_end - kernel_base;
  KDEBUG_LOG(
      "Reserving kernel: 0x%llx - 0x%llx (%llu KB, includes stack and page "
      "tables in .bss)",
      (unsigned long long)kernel_base, (unsigned long long)kernel_end,
      (unsigned long long)(kernel_size / 1024));
  subtract_reserved_region(platform->mem_regions, &head, &tail,
                           &platform->num_mem_regions, kernel_base,
                           kernel_size);

  // Store the head and tail pointers for O(1) access in platform_mem_regions
  platform->mem_regions_head = head;
  platform->mem_regions_tail = tail;

  // Print final free regions
  KLOG("Free memory regions:");
  size_t total_free = 0;
  region = head;
  idx = 0;
  while (region != NULL) {
    KLOG("Region %d: 0x%llx - 0x%llx (%llu MB)", idx,
         (unsigned long long)region->base,
         (unsigned long long)(region->base + region->size),
         (unsigned long long)(region->size / 1024 / 1024));
    total_free += region->size;
    region = region->next;
    idx++;
  }
  KLOG("Total free memory: %llu MB",
       (unsigned long long)(total_free / 1024 / 1024));
}

// Initialize memory management and MMU
void platform_mem_init(platform_t *platform) {
  setup_mmu(platform);
  build_free_regions(platform);
  KDEBUG_VALIDATE(platform_mem_debug_mmu());
}

// Get list of available memory regions
kregions_t platform_mem_regions(platform_t *platform) {
  kregions_t list;
  list.count = platform->num_mem_regions;
  list.head = platform->mem_regions_head;
  list.tail = platform->mem_regions_tail;
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

// =============================================================================
// Platform Initialization (from platform_init.c)
// =============================================================================

static void wfi_timer_callback(void) {}

// Platform-specific initialization
void platform_init(platform_t *platform, void *fdt, void *kernel) {
  KLOG("arm64 init...");

  memset(platform, 0, sizeof(platform_t));
  platform->kernel = kernel;

  KDEBUG_VALIDATE(platform_fdt_dump(platform, fdt));

  KLOG("fdt parse...");
  platform_boot_context_parse(platform, fdt);
  KLOG("fdt parse ok");

  KLOG("uart init...");
  platform_uart_init(platform);
  KLOG("uart init ok");

  KLOG("mem init...");
  platform_mem_init(platform);
  KLOG("mem init ok");

  KLOG("interrupt init...");
  interrupt_init(platform);
  KLOG("interrupt init ok");

  KLOG("timer init...");
  timer_init(platform);
  KLOG("timer init ok");

  KLOG("virtio scan...");
  pci_scan_devices(platform);
  mmio_scan_devices(platform);
  KLOG("virtio scan ok");

  KLOG("arm64 init ok");
}

// Wait for interrupt with timeout
// timeout_ns: timeout in nanoseconds (UINT64_MAX = wait forever)
// Returns: current time in nanoseconds
ktime_t platform_wfi(platform_t *platform, ktime_t timeout_ns) {
  if (timeout_ns == 0) {
    return timer_get_current_time_ns(platform);
  }

  // Disable interrupts to check condition atomically
  __asm__ volatile("msr daifset, #2" ::: "memory"); // Disable IRQs

  // Check if an interrupt has already fired (ring buffer not empty)
  if (!kirq_ring_is_empty(&platform->irq_ring)) {
    __asm__ volatile("msr daifclr, #2" ::: "memory"); // Re-enable IRQs
    return timer_get_current_time_ns(platform);
  }

  // Set timeout timer if not UINT64_MAX
  if (timeout_ns != UINT64_MAX) {
    // Convert nanoseconds to milliseconds for timer_set_oneshot_ms
    uint64_t timeout_ms = timeout_ns / 1000000ULL;
    // For timeouts > UINT32_MAX ms, cap at UINT32_MAX
    uint32_t timeout_ms_32 =
        (timeout_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)timeout_ms;
    timer_set_oneshot_ms(platform, timeout_ms_32, wfi_timer_callback);
  }

  // Atomically enable interrupts and wait
  __asm__ volatile("wfi" ::: "memory");
  __asm__ volatile("msr daifclr, #2" ::: "memory");

  // Cancel timer if it was set
  if (timeout_ns != UINT64_MAX) {
    timer_cancel(platform);
  }

  // Return current time
  return timer_get_current_time_ns(platform);
}

// Abort system execution (shutdown/halt)
void platform_abort(void) {
  // Disable interrupts
  __asm__ volatile("msr daifset, #2" ::: "memory"); // Disable IRQs
  // Trigger undefined instruction exception (causes QEMU to exit)
  // Using .word directive for undefined instruction
  __asm__ volatile(".word 0x00000000" ::: "memory");
  // Should never reach here, but halt just in case
  while (1) {
    __asm__ volatile("wfi" ::: "memory");
  }
}

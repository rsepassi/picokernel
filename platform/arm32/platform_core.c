// ARM32 Platform Core Initialization
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

// PCI config space register offsets
#define PCI_REG_INTERRUPT_PIN 0x3D

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
// ARM32 MMU Configuration Constants (4KB pages, short-descriptor format)
// =============================================================================

// ARMv7-A short-descriptor format:
// L1 table (4096 entries): Each entry covers 1 MB
// L2 table (256 entries): Each entry covers 4 KB

#define ARM32_L1_SHIFT 20  // L1 entry covers 1 MB
#define ARM32_L2_SHIFT 12  // L2 entry covers 4 KB

#define ARM32_L1_COVERAGE (1UL << ARM32_L1_SHIFT) // 1 MB
#define ARM32_L2_COVERAGE (1UL << ARM32_L2_SHIFT) // 4 KB

// Index masks
#define ARM32_L1_MASK 0xFFFUL  // 12 bits for L1 index (4096 entries)
#define ARM32_L2_MASK 0xFFUL   // 8 bits for L2 index (256 entries)

// Page alignment mask (4KB pages)
#define ARM32_PAGE_ALIGN_MASK 0xFFFUL // Lower 12 bits for 4KB pages

#define ARM32_L1_INDEX(addr) (((addr) >> ARM32_L1_SHIFT) & ARM32_L1_MASK)
#define ARM32_L2_INDEX(addr) (((addr) >> ARM32_L2_SHIFT) & ARM32_L2_MASK)

// L1 descriptor types (bits [1:0])
#define L1_TYPE_FAULT      0x0  // Invalid
#define L1_TYPE_PAGE_TABLE 0x1  // Points to L2 table
#define L1_TYPE_SECTION    0x2  // 1 MB section mapping

// L2 descriptor types (bits [1:0])
#define L2_TYPE_FAULT      0x0  // Invalid
#define L2_TYPE_LARGE_PAGE 0x1  // 64 KB large page (not used)
#define L2_TYPE_SMALL_PAGE 0x2  // 4 KB small page (extended)

// L1 page table descriptor flags
#define L1_PAGE_TABLE_NS   (1UL << 3)  // Non-secure (bit 3)

// L2 small page flags (extended format)
#define L2_SMALL_PAGE_XN     (1UL << 0)  // Execute Never
#define L2_SMALL_PAGE_B      (1UL << 2)  // Bufferable
#define L2_SMALL_PAGE_C      (1UL << 3)  // Cacheable
#define L2_SMALL_PAGE_AP0    (1UL << 4)  // Access Permission bit 0
#define L2_SMALL_PAGE_AP1    (1UL << 5)  // Access Permission bit 1
#define L2_SMALL_PAGE_TEX0   (1UL << 6)  // Type Extension bit 0
#define L2_SMALL_PAGE_TEX1   (1UL << 7)  // Type Extension bit 1
#define L2_SMALL_PAGE_TEX2   (1UL << 8)  // Type Extension bit 2
#define L2_SMALL_PAGE_AP2    (1UL << 9)  // Access Permission bit 2
#define L2_SMALL_PAGE_S      (1UL << 10) // Shareable
#define L2_SMALL_PAGE_NG     (1UL << 11) // Not Global

// Normal memory: TEX=001, C=1, B=1 (Write-Back, Write-Allocate)
#define L2_SMALL_PAGE_NORMAL (L2_TYPE_SMALL_PAGE | L2_SMALL_PAGE_TEX0 | \
                               L2_SMALL_PAGE_C | L2_SMALL_PAGE_B | \
                               L2_SMALL_PAGE_AP0 | L2_SMALL_PAGE_AP1 | \
                               L2_SMALL_PAGE_S)

// Device memory: TEX=000, C=0, B=1 (Shared Device)
#define L2_SMALL_PAGE_DEVICE (L2_TYPE_SMALL_PAGE | L2_SMALL_PAGE_B | \
                               L2_SMALL_PAGE_AP0 | L2_SMALL_PAGE_AP1 | \
                               L2_SMALL_PAGE_XN | L2_SMALL_PAGE_S)

// =============================================================================
// FDT Parsing (adapted from arm64)
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
void platform_boot_context_parse(platform_t *platform, void *boot_context) {
  void *fdt = boot_context;

  KDEBUG_LOG("ARM32 FDT parsing");
  KASSERT(fdt, "FDT is NULL");

  // Validate FDT header
  int err = fdt_check_header(fdt);
  KASSERT(err == 0, "Invalid FDT header");

  // Store FDT location and size
  platform->fdt_base = (uintptr_t)fdt;
  platform->fdt_size = fdt_totalsize(fdt);
  KDEBUG_LOG("FDT size: %u bytes", platform->fdt_size);
  // Align size up to 4KB page boundary
  platform->fdt_size = KALIGN(platform->fdt_size, ARM32_PAGE_SIZE);

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
        // QEMU virt uses 2-cell addresses (64-bit) for all registers
        if (reg && len >= 16) {
          kregion_t *region = &platform->mem_regions[platform->num_mem_regions];
          region->base = kload_be64((const uint8_t *)reg);
          region->size = kload_be64((const uint8_t *)reg + 8);

          // Link into doubly-linked list
          region->next = NULL;
          if (platform->num_mem_regions == 0) {
            region->prev = NULL;
          } else {
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
        // GIC CPU interface is second register pair (each pair is 16 bytes)
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
          for (int offset = 0; offset + 28 <= len; offset += 28) {
            const uint8_t *entry = ranges + offset;
            uint32_t flags = kload_be32(entry);
            uint64_t parent_addr = kload_be64(entry + 12);
            uint64_t size = kload_be64(entry + 20);

            uint32_t space_code = flags & 0x03000000;
            if (space_code == 0x03000000 || space_code == 0x02000000) {
              platform->pci_mmio_base = parent_addr;
              platform->pci_mmio_size = size;
              break;
            }
          }
        }
      }
    }

    // Generic MMIO region collection
    if (depth == 2 && platform->num_mmio_regions < KCONFIG_MAX_MMIO_REGIONS) {
      if (memcmp(name, "memory@", 7) == 0 || strcmp(name, "memory") == 0) {
        continue;
      }

      int reg_len, compat_len;
      const void *reg = fdt_getprop(fdt, node, "reg", &reg_len);
      const void *compat = fdt_getprop(fdt, node, "compatible", &compat_len);

      if (reg && compat && reg_len >= 16) {
        // QEMU virt machine uses 2-cell addresses (64-bit) even for ARM32
        uint64_t base = kload_be64((const uint8_t *)reg);
        uint64_t size = kload_be64((const uint8_t *)reg + 8);

        // Align size up to ARM32_PAGE_SIZE (4KB)
        size = KALIGN(size, ARM32_PAGE_SIZE);

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

  // Initialize BAR allocator
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
// MMU Setup and Memory Region Management (adapted from arm64)
// =============================================================================

// Helper: Check if two memory ranges overlap
static int ranges_overlap(uintptr_t base1, size_t size1, uintptr_t base2,
                          size_t size2) {
  uintptr_t end1 = base1 + size1;
  uintptr_t end2 = base2 + size2;
  return !(end1 <= base2 || end2 <= base1);
}

// Helper: Subtract reserved region from available regions
static void subtract_reserved_region(kregion_t *regions, kregion_t **head,
                                     kregion_t **tail, int *count,
                                     uintptr_t reserved_base,
                                     size_t reserved_size) {
  uintptr_t reserved_end = reserved_base + reserved_size;

  kregion_t *region = *head;
  while (region != NULL) {
    kregion_t *next = region->next;
    uintptr_t region_base = region->base;
    uintptr_t region_end = region_base + region->size;

    if (!ranges_overlap(region_base, region->size, reserved_base,
                        reserved_size)) {
      region = next;
      continue;
    }

    // Case 1: Reserved completely contains region
    if (reserved_base <= region_base && reserved_end >= region_end) {
      if (region->prev != NULL) {
        region->prev->next = region->next;
      } else {
        *head = region->next;
      }
      if (region->next != NULL) {
        region->next->prev = region->prev;
      } else {
        *tail = region->prev;
      }
      (*count)--;
      region = next;
      continue;
    }

    // Case 2: Reserved at the start
    if (reserved_base <= region_base && reserved_end < region_end) {
      region->base = reserved_end;
      region->size = region_end - reserved_end;
      region = next;
      continue;
    }

    // Case 3: Reserved at the end
    if (reserved_base > region_base && reserved_end >= region_end) {
      region->size = reserved_base - region_base;
      region = next;
      continue;
    }

    // Case 4: Reserved in the middle - split
    if (reserved_base > region_base && reserved_end < region_end) {
      region->size = reserved_base - region_base;

      if (*count < KCONFIG_MAX_MEM_REGIONS) {
        kregion_t *new_region = &regions[*count];
        new_region->base = reserved_end;
        new_region->size = region_end - reserved_end;
        new_region->prev = region;
        new_region->next = region->next;

        if (region->next != NULL) {
          region->next->prev = new_region;
        } else {
          *tail = new_region;
        }
        region->next = new_region;

        (*count)++;
      }
    }

    region = next;
  }
}

// Helper: Allocate an L2 table from the pool
static uint32_t *get_l2_table(platform_t *platform) {
  KASSERT(platform->next_l2_table < KCONFIG_ARM32_MAX_L2_TABLES,
          "L2 table pool exhausted");
  return platform->page_table_l2_pool[platform->next_l2_table++];
}

// Helper: Map a single 4KB page with identity mapping
static void map_page(platform_t *platform, uint32_t phys_addr, uint32_t flags) {
  // Physical address must be 4KB aligned
  KASSERT((phys_addr & ARM32_PAGE_ALIGN_MASK) == 0,
          "Physical address must be 4KB aligned for mapping");

  uint32_t l1_idx = ARM32_L1_INDEX(phys_addr);
  uint32_t l2_idx = ARM32_L2_INDEX(phys_addr);

  // Ensure L1 → L2 mapping exists
  if ((platform->page_table_l1[l1_idx] & 0x3) != L1_TYPE_PAGE_TABLE) {
    uint32_t *l2_table = get_l2_table(platform);
    uint32_t l2_addr = (uint32_t)l2_table;
    KASSERT((l2_addr & 0x3FF) == 0, "L2 table must be 1KB aligned");
    platform->page_table_l1[l1_idx] = l2_addr | L1_TYPE_PAGE_TABLE;
  }

  // Get L2 table
  uint32_t *l2_table = (uint32_t *)(platform->page_table_l1[l1_idx] & ~0x3FF);

  // Skip if page is already mapped
  if ((l2_table[l2_idx] & 0x3) == L2_TYPE_SMALL_PAGE) {
    uint32_t existing_entry = l2_table[l2_idx];
    uint32_t new_entry = phys_addr | flags;
    KASSERT(existing_entry == new_entry, "Conflicting mapping flags");
    return;
  }

  l2_table[l2_idx] = phys_addr | flags;
}

// Helper: Map a range of pages with the same flags
static void map_page_range(platform_t *platform, uint32_t base, uint32_t size,
                           uint32_t flags) {
  uint32_t base_aligned = KALIGN_BACK(base, ARM32_PAGE_SIZE);
  uint32_t end = base + size;
  uint32_t end_aligned = KALIGN(end, ARM32_PAGE_SIZE);
  uint32_t size_aligned = end_aligned - base_aligned;

  for (uint32_t addr = base_aligned; addr < base_aligned + size_aligned;
       addr += ARM32_PAGE_SIZE) {
    map_page(platform, addr, flags);
  }
}

// Setup MMU with identity mapping
__attribute__((noinline)) static void setup_mmu(platform_t *platform) {
  KDEBUG_LOG("Setting up ARM32 MMU (4KB pages, short-descriptor format)...");

  // Print kernel location
  KDEBUG_LOG("Kernel location: _start=0x%08x, _end=0x%08x, size=%u KB",
             (unsigned int)(uintptr_t)_start,
             (unsigned int)(uintptr_t)_end,
             (unsigned int)((uintptr_t)_end - (uintptr_t)_start) / 1024);

  // Reset allocation counter
  platform->next_l2_table = 0;

  // Clear all page tables
  for (int i = 0; i < 4096; i++) {
    platform->page_table_l1[i] = 0;
  }
  for (int t = 0; t < KCONFIG_ARM32_MAX_L2_TABLES; t++) {
    for (int i = 0; i < 256; i++) {
      platform->page_table_l2_pool[t][i] = 0;
    }
  }

  // Map MMIO regions discovered from FDT
  // All device MMIO regions are discovered during FDT parsing and stored in
  // platform->mmio_regions[]. We map each region individually with device
  // memory attributes.
  KDEBUG_LOG("Mapping %d MMIO regions from FDT:", platform->num_mmio_regions);
  for (int r = 0; r < platform->num_mmio_regions; r++) {
    uint32_t mmio_base = (uint32_t)platform->mmio_regions[r].base;
    uint32_t mmio_size = (uint32_t)platform->mmio_regions[r].size;

    KDEBUG_LOG("  Region %d: 0x%08x - 0x%08x (0x%08x bytes)", r, mmio_base,
               mmio_base + mmio_size, mmio_size);

    map_page_range(platform, mmio_base, mmio_size, L2_SMALL_PAGE_DEVICE);
  }

  // Map UART explicitly (it's parsed from FDT but not in MMIO regions list)
  if (platform->uart_base != 0) {
    KDEBUG_LOG("Mapping UART: 0x%08x", (unsigned int)platform->uart_base);
    map_page_range(platform, platform->uart_base, 0x1000,
                   L2_SMALL_PAGE_DEVICE);
  }

  // Map GIC explicitly (parsed from FDT but not in MMIO regions list)
  if (platform->gic_dist_base != 0) {
    KDEBUG_LOG("Mapping GIC Distributor: 0x%08x",
               (unsigned int)platform->gic_dist_base);
    map_page_range(platform, platform->gic_dist_base, 0x1000,
                   L2_SMALL_PAGE_DEVICE);
  }
  if (platform->gic_cpu_base != 0) {
    KDEBUG_LOG("Mapping GIC CPU Interface: 0x%08x",
               (unsigned int)platform->gic_cpu_base);
    map_page_range(platform, platform->gic_cpu_base, 0x1000,
                   L2_SMALL_PAGE_DEVICE);
  }

  // Map PCI MMIO region explicitly (for device BARs)
  // Note: This is separate from PCI ECAM (configuration space)
  // Limit to 128 MB to avoid exhausting L2 table pool
  if (platform->pci_mmio_base != 0) {
    uint32_t pci_mmio_size = (uint32_t)platform->pci_mmio_size;
    if (pci_mmio_size > 128 * 1024 * 1024) {
      KDEBUG_LOG("Limiting PCI MMIO from %u MB to 128 MB",
                 pci_mmio_size / 1024 / 1024);
      pci_mmio_size = 128 * 1024 * 1024;
    }
    KDEBUG_LOG("Mapping PCI MMIO: 0x%08x (size: 0x%08x)",
               (unsigned int)platform->pci_mmio_base, pci_mmio_size);
    map_page_range(platform, (uint32_t)platform->pci_mmio_base, pci_mmio_size,
                   L2_SMALL_PAGE_DEVICE);
  }

  // Map RAM regions (limit to what QEMU actually provides: 128 MB)
  for (int r = 0; r < platform->num_mem_regions; r++) {
    uint32_t ram_base = (uint32_t)platform->mem_regions[r].base;
    uint32_t ram_size = (uint32_t)platform->mem_regions[r].size;

    // Limit to 128 MB (what QEMU actually gives us with -m 128M)
    // The FDT may report a larger region but it's not all backed by actual RAM
    if (ram_size > 128 * 1024 * 1024) {
      KDEBUG_LOG("Limiting RAM region %d from %u MB to 128 MB", r,
                 ram_size / 1024 / 1024);
      ram_size = 128 * 1024 * 1024;
    }

    KDEBUG_LOG("Mapping RAM region %d: 0x%08x - 0x%08x (%u MB)", r, ram_base,
               ram_base + ram_size, ram_size / 1024 / 1024);

    map_page_range(platform, ram_base, ram_size, L2_SMALL_PAGE_NORMAL);
  }

  KDEBUG_LOG("MMU setup complete, configuring registers...");

  // Set domain access control (Domain 0 = client, all others = no access)
  uint32_t dacr = 0x00000001;
  __asm__ volatile("mcr p15, 0, %0, c3, c0, 0" : : "r"(dacr));

  // Set TTBR0 (Translation Table Base Register 0)
  uint32_t ttbr0 = (uint32_t)platform->page_table_l1;
  __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" : : "r"(ttbr0));

  // Set TTBCR (Translation Table Base Control Register)
  // N=0: Use TTBR0 for all addresses
  uint32_t ttbcr = 0;
  __asm__ volatile("mcr p15, 0, %0, c2, c0, 2" : : "r"(ttbcr));

  // Flush writes
  __asm__ volatile("dsb" ::: "memory");

  // Invalidate TLB (Translation Lookaside Buffer)
  __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));
  __asm__ volatile("dsb" ::: "memory");

  // Invalidate entire instruction cache
  __asm__ volatile("mcr p15, 0, %0, c7, c5, 0" : : "r"(0));
  __asm__ volatile("dsb" ::: "memory");
  __asm__ volatile("isb" ::: "memory");

  KDEBUG_LOG("MMU setup: caches invalidated, barriers complete");

  // Enable MMU first
  uint32_t sctlr;
  __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
  KDEBUG_LOG("MMU setup: enabling MMU...");
  sctlr |= (1 << 0); // M: Enable MMU
  __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" : : "r"(sctlr));
  __asm__ volatile("isb" ::: "memory");

  // Now enable caches
  sctlr |= (1 << 2);  // C: Enable data cache
  sctlr |= (1 << 12); // I: Enable instruction cache
  __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" : : "r"(sctlr));
  __asm__ volatile("isb" ::: "memory");

  KDEBUG_LOG("MMU enabled with caching");
}

// Build free memory region list
__attribute__((noinline)) static void build_free_regions(platform_t *platform) {
  KDEBUG_LOG("Building free memory region list...");

  kregion_t *head =
      (platform->num_mem_regions > 0) ? &platform->mem_regions[0] : NULL;
  kregion_t *tail = NULL;

  if (head != NULL) {
    tail = head;
    while (tail->next != NULL) {
      tail = tail->next;
    }
  }

  // Reserve DTB region
  if (platform->fdt_base != 0 && platform->fdt_size != 0) {
    KDEBUG_LOG("Reserving DTB: 0x%08x - 0x%08x (%u KB)",
               (unsigned int)platform->fdt_base,
               (unsigned int)(platform->fdt_base + platform->fdt_size),
               (unsigned int)(platform->fdt_size / 1024));
    subtract_reserved_region(platform->mem_regions, &head, &tail,
                             &platform->num_mem_regions, platform->fdt_base,
                             platform->fdt_size);
  }

  // Reserve kernel region
  uintptr_t kernel_base = (uintptr_t)_start;
  uintptr_t kernel_end = (uintptr_t)_end;
  size_t kernel_size = kernel_end - kernel_base;
  KDEBUG_LOG("Reserving kernel: 0x%08x - 0x%08x (%u KB)",
             (unsigned int)kernel_base, (unsigned int)kernel_end,
             (unsigned int)(kernel_size / 1024));
  subtract_reserved_region(platform->mem_regions, &head, &tail,
                           &platform->num_mem_regions, kernel_base,
                           kernel_size);

  platform->mem_regions_head = head;
  platform->mem_regions_tail = tail;

  // Print final free regions
  KLOG("Free memory regions:");
  size_t total_free = 0;
  kregion_t *region = head;
  int idx = 0;
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

// Debug: Verify MMU configuration
void platform_mem_debug_mmu(void) {
  printk("\n=== ARM32 MMU Configuration Debug ===\n");

  // Read SCTLR
  uint32_t sctlr;
  __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
  printk("SCTLR: 0x");
  printk_hex32(sctlr);
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

  // Read TTBR0
  uint32_t ttbr0;
  __asm__ volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(ttbr0));
  printk("TTBR0: 0x");
  printk_hex32(ttbr0);
  printk("\n");

  // Read DACR
  uint32_t dacr;
  __asm__ volatile("mrc p15, 0, %0, c3, c0, 0" : "=r"(dacr));
  printk("DACR: 0x");
  printk_hex32(dacr);
  printk("\n");

  printk("=== End MMU Debug ===\n\n");
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

// =============================================================================
// Platform Initialization (from platform_init.c)
// =============================================================================

static void wfi_timer_callback(void) {}

// Platform-specific initialization
void platform_init(platform_t *platform, void *fdt, void *kernel) {
  KLOG("arm32 init...");

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

  KLOG("arm32 init ok");
}

// Wait for interrupt with timeout
ktime_t platform_wfi(platform_t *platform, ktime_t timeout_ns) {
  if (timeout_ns == 0) {
    return timer_get_current_time_ns(platform);
  }

  // Disable interrupts to check condition atomically
  __asm__ volatile("cpsid i" ::: "memory");

  // Check if an interrupt has already fired
  if (!kirq_ring_is_empty(&platform->irq_ring)) {
    __asm__ volatile("cpsie i" ::: "memory");
    return timer_get_current_time_ns(platform);
  }

  // Set timeout timer if not UINT64_MAX
  if (timeout_ns != UINT64_MAX) {
    uint64_t timeout_ms = timeout_ns / 1000000ULL;
    uint32_t timeout_ms_32 =
        (timeout_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)timeout_ms;
    timer_set_oneshot_ms(platform, timeout_ms_32, wfi_timer_callback);
  }

  // Wait for interrupt (WFI wakes on pending IRQ even if masked)
  __asm__ volatile("wfi" ::: "memory");

  // Re-enable interrupts after WFI
  __asm__ volatile("cpsie i" ::: "memory");

  // Cancel timer if it was set
  if (timeout_ns != UINT64_MAX) {
    timer_cancel(platform);
  }

  return timer_get_current_time_ns(platform);
}

// Abort system execution
void platform_abort(void) {
  __asm__ volatile("cpsid i" ::: "memory");
  __asm__ volatile(".word 0xe7f000f0" ::: "memory");
  while (1) {
    __asm__ volatile("wfi" ::: "memory");
  }
}

// =============================================================================
// Device Discovery (platform.h contract)
// =============================================================================

// Discover MMIO devices via hardcoded probing (platform.h contract)
// Probes known MMIO address range for VirtIO devices and reads their device IDs
int platform_discover_mmio_devices(platform_t *platform,
                                   platform_mmio_device_t *devices,
                                   int max_devices) {
  // Get MMIO base address from platform or use default
  uint64_t mmio_base = platform->virtio_mmio_base;
  if (mmio_base == 0) {
    mmio_base = VIRTIO_MMIO_BASE;
  }

  int valid_count = 0;

  // Scan for devices at known MMIO slots
  for (int i = 0; i < VIRTIO_MMIO_MAX_DEVICES && valid_count < max_devices;
       i++) {
    uint64_t base = mmio_base + (i * VIRTIO_MMIO_DEVICE_STRIDE);

    // Read magic value at offset 0x00
    volatile uint32_t *magic_ptr = (volatile uint32_t *)base;
    uint32_t magic = *magic_ptr;

    // VirtIO magic value is 0x74726976 ("virt" in little-endian)
    if (magic != VIRTIO_MMIO_MAGIC) {
      continue; // No device at this address
    }

    // Read device ID at offset 0x08
    volatile uint32_t *device_id_ptr =
        (volatile uint32_t *)(base + VIRTIO_MMIO_DEVICE_ID);
    uint32_t device_id = *device_id_ptr;

    // Device ID 0 means empty slot
    if (device_id == 0) {
      continue;
    }

    // Calculate IRQ number using platform-specific function
    uint32_t irq_num = platform_mmio_irq_number(platform, i);

    // Fill platform-independent device descriptor
    devices[valid_count].mmio_base = base;
    devices[valid_count].mmio_size = VIRTIO_MMIO_DEVICE_STRIDE;
    devices[valid_count].irq_num = irq_num;
    devices[valid_count].device_id = device_id;
    devices[valid_count].valid = true;
    valid_count++;
  }

  return valid_count;
}

// Configure PCI device interrupts (ARM32: legacy INTx)
// Returns hardware IRQ number to use for interrupt registration
int platform_pci_setup_interrupts(platform_t *platform, uint8_t bus,
                                  uint8_t slot, uint8_t func,
                                  void *transport) {
  (void)bus;       // Unused on ARM32
  (void)transport; // Not needed for INTx

  // Read PCI interrupt pin from config space
  uint8_t irq_pin = platform_pci_config_read8(platform, bus, slot, func,
                                              PCI_REG_INTERRUPT_PIN);

  // Calculate hardware IRQ number using platform swizzling
  uint32_t irq_num = platform_pci_irq_swizzle(platform, slot, irq_pin);

  return (int)irq_num;
}

// RISC-V 64-bit Platform Core Initialization
// Combines: FDT parsing, MMU setup, and initialization orchestration

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kbase.h"
#include "kconfig.h"
#include "platform.h"
#include "platform_impl.h"
#include "printk.h"

#include "interrupt.h"
#include "sbi.h"
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
void platform_uart_init(platform_t *platform);
void platform_boot_context_parse(platform_t *platform, void *boot_context);
void platform_mem_init(platform_t *platform);

// Linker-provided symbols
extern uint8_t _text_start[];
extern uint8_t _end[];
extern uint8_t stack_bottom[];
extern uint8_t stack_top[];

// =============================================================================
// RISC-V Sv39 MMU Configuration Constants (4KB pages, 39-bit VA)
// =============================================================================

// Sv39 uses 3-level page tables with 4KB pages
// L2 (root): 512 entries, each covers 1 GB
// L1: 512 entries, each covers 2 MB
// L0 (leaf): 512 entries, each covers 4 KB

#define RV64_PAGE_SHIFT 12
#define RV64_PAGE_SIZE (1ULL << RV64_PAGE_SHIFT) // 4 KB

// Page table entry flags
#define PTE_V (1UL << 0) // Valid
#define PTE_R (1UL << 1) // Readable
#define PTE_W (1UL << 2) // Writable
#define PTE_X (1UL << 3) // Executable
#define PTE_A (1UL << 6) // Accessed
#define PTE_D (1UL << 7) // Dirty

// Page table pointer (non-leaf): V=1, R=W=X=0
#define PTE_TABLE (PTE_V)

// Normal RAM: readable, writable, executable, accessed, dirty
#define PTE_RAM (PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D)

// Device MMIO: readable, writable, accessed, dirty (no execute)
#define PTE_MMIO (PTE_V | PTE_R | PTE_W | PTE_A | PTE_D)

// satp register fields (for Sv39)
#define SATP_MODE_SV39 (8UL << 60) // Mode = Sv39

// Page tables (static allocation in BSS)
// Using 2 MB megapages, we need:
// - 1 L2 table (root): 512 entries, each covers 1 GB
// - 512 L1 tables: one per L2 entry, full Sv39 address space coverage (512 GB)
// Total allocation: 512 × 4 KB = 2 MB for complete address space support
static uint64_t pt_l2[512] __attribute__((aligned(4096)));
static uint64_t pt_l1[512][512] __attribute__((aligned(4096))); // 2 MB total

// =============================================================================
// FDT Parsing
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

  KDEBUG_LOG("RISC-V FDT parsing");
  KASSERT(fdt, "FDT is NULL");

  // Validate FDT header
  int err = fdt_check_header(fdt);
  KASSERT(err == 0, "Invalid FDT header");

  // Store FDT location and size
  platform->fdt_base = (uintptr_t)fdt;
  platform->fdt_size = fdt_totalsize(fdt);
  KDEBUG_LOG("FDT size: %u bytes", platform->fdt_size);
  // Align size up to 4KB page boundary
  platform->fdt_size = KALIGN(platform->fdt_size, RV64_PAGE_SIZE);

  // Initialize platform memory regions
  platform->num_mem_regions = 0;

  // Initialize MMIO regions
  platform->num_mmio_regions = 0;

  // Initialize device addresses to zero
  platform->uart_base = 0;
  platform->plic_base = 0;
  platform->clint_base = 0;
  platform->pci_ecam_base = 0;
  platform->pci_ecam_size = 0;
  platform->pci_mmio_base = 0;
  platform->pci_mmio_size = 0;
  platform->virtio_mmio_base = 0;

  KDEBUG_LOG("Traversing device tree");

  // Compatible string lists for device matching
  const char *uart_compat[] = {"ns16550a", "ns16550", "sifive,uart0", NULL};
  const char *plic_compat[] = {"riscv,plic0", "sifive,plic-1.0.0", NULL};
  const char *clint_compat[] = {"riscv,clint0", "sifive,clint0", NULL};
  const char *pci_compat[] = {"pci-host-ecam-generic", NULL};

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

    // Check for PLIC (Platform-Level Interrupt Controller)
    if (platform->plic_base == 0 && compatible_match(fdt, node, plic_compat)) {
      int len;
      const void *reg = fdt_getprop(fdt, node, "reg", &len);
      if (reg && len >= 16) {
        platform->plic_base = kload_be64((const uint8_t *)reg);
      }
      continue;
    }

    // Check for CLINT (Core-Local Interruptor)
    if (platform->clint_base == 0 &&
        compatible_match(fdt, node, clint_compat)) {
      int len;
      const void *reg = fdt_getprop(fdt, node, "reg", &len);
      if (reg && len >= 16) {
        platform->clint_base = kload_be64((const uint8_t *)reg);
      }
      continue;
    }

    // Check for PCI ECAM
    if (platform->pci_ecam_base == 0 &&
        compatible_match(fdt, node, pci_compat)) {
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

          // Check if this is 64-bit MMIO space (0x03000000 = prefetchable,
          // 0x02000000 = 32-bit MMIO, 0x43000000 = 64-bit non-prefetchable)
          uint32_t space_code = flags & 0x03000000;
          if (space_code == 0x03000000 || space_code == 0x02000000) {
            // Found MMIO space - use this for BAR allocation
            platform->pci_mmio_base = parent_addr;
            platform->pci_mmio_size = size;
            break; // Use the first MMIO range we find
          }
        }
      }
      continue;
    }

    // Check for VirtIO MMIO (track minimum device base address for scanning)
    {
      static const char *virtio_mmio_compat[] = {"virtio,mmio", NULL};
      if (compatible_match(fdt, node, virtio_mmio_compat)) {
        int len;
        const void *reg = fdt_getprop(fdt, node, "reg", &len);
        if (reg && len >= 16) {
          uint64_t base = kload_be64((const uint8_t *)reg);
          // Track the minimum VirtIO MMIO address (first device in range)
          if (platform->virtio_mmio_base == 0 || base < platform->virtio_mmio_base) {
            platform->virtio_mmio_base = base;
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

        // Align size up to RV64_PAGE_SIZE (4KB)
        size = KALIGN(size, RV64_PAGE_SIZE);

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

  if (platform->plic_base != 0) {
    KLOG("  PLIC: 0x%llx", (unsigned long long)platform->plic_base);
  }

  if (platform->clint_base != 0) {
    KLOG("  CLINT: 0x%llx", (unsigned long long)platform->clint_base);
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

  if (platform->virtio_mmio_base != 0) {
    KLOG("  VirtIO MMIO: 0x%llx",
         (unsigned long long)platform->virtio_mmio_base);
  }

  // Initialize BAR allocator for bare-metal PCI device configuration
  // Use FDT-discovered MMIO range if available, otherwise default to 0x40000000
  platform->pci_next_bar_addr =
      platform->pci_mmio_base ? platform->pci_mmio_base : 0x40000000;

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
// MMU Setup and Memory Region Management
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

// Helper function to map a memory range using 2MB megapages
static void map_mmio_range(uint64_t base, uint64_t size, const char *name __attribute__((unused))) {
    KDEBUG_LOG("Mapping %s: 0x%llx - 0x%llx (0x%llx bytes)", name,
               (unsigned long long)base,
               (unsigned long long)(base + size),
               (unsigned long long)size);

    // Map this MMIO region using 2MB megapages
    // Calculate L2 index (which 1 GB region this falls in)
    uint64_t l2_idx = (base >> 30) & 0x1FF;

    // Setup L2 entry if not already set
    if (pt_l2[l2_idx] == 0) {
      pt_l2[l2_idx] = ((uint64_t)pt_l1[l2_idx] >> 12) << 10 | PTE_TABLE;
    }

    // Map each 2MB page in this region
    uint64_t start_addr = base & ~0x1FFFFF; // Align down to 2MB
    uint64_t end_addr = KALIGN(base + size, 0x200000); // Align up
    for (uint64_t addr = start_addr; addr < end_addr; addr += 0x200000) {
      uint64_t l1_idx = (addr >> 21) & 0x1FF; // L1 index within the 1GB slot
      // Skip if already mapped (handles overlapping regions from FDT)
      if (pt_l1[l2_idx][l1_idx] & PTE_V) {
        continue;
      }
      pt_l1[l2_idx][l1_idx] = (addr >> 12) << 10 | PTE_MMIO;
    }
}

// Setup MMU with Sv39 identity mapping
__attribute__((noinline)) static void setup_mmu(platform_t *platform) {
  KDEBUG_LOG("Setting up RISC-V Sv39 MMU (4KB pages, 39-bit address space)...");

  // Print kernel location for debugging
  KDEBUG_LOG("Kernel location: _text_start=0x%llx, _end=0x%llx, size=%llu KB",
             (unsigned long long)(uintptr_t)_text_start,
             (unsigned long long)(uintptr_t)_end,
             (unsigned long long)((uintptr_t)_end - (uintptr_t)_text_start) /
                 1024);

  // Check if MMU is already enabled
  uint64_t satp;
  __asm__ volatile("csrr %0, satp" : "=r"(satp));
  uint64_t mode = satp >> 60;
  if (mode != 0) {
    KDEBUG_LOG("MMU already enabled, skipping setup");
    return;
  }

  // Zero out page tables
  for (int i = 0; i < 512; i++) {
    pt_l2[i] = 0;
    for (int j = 0; j < 512; j++) {
      pt_l1[i][j] = 0;
    }
  }

  // Map MMIO regions discovered from FDT
  KDEBUG_LOG("Mapping %d MMIO regions from FDT:", platform->num_mmio_regions);
  for (int r = 0; r < platform->num_mmio_regions; r++) {
    uint64_t mmio_base = platform->mmio_regions[r].base;
    uint64_t mmio_size = platform->mmio_regions[r].size;
    map_mmio_range(mmio_base, mmio_size, "MMIO region");
  }

  // Map UART explicitly (it's parsed from FDT but might not be in MMIO regions list)
  if (platform->uart_base != 0) {
    map_mmio_range(platform->uart_base, 0x10000, "UART");
  }

  // Map PLIC explicitly (parsed from FDT but not in MMIO regions list)
  if (platform->plic_base != 0) {
    map_mmio_range(platform->plic_base, 0x400000, "PLIC");
  }

  // Map CLINT explicitly (parsed from FDT but not in MMIO regions list)
  if (platform->clint_base != 0) {
    map_mmio_range(platform->clint_base, 0x10000, "CLINT");
  }

  // Map PCI ECAM explicitly (for device configuration space)
  if (platform->pci_ecam_base != 0) {
    map_mmio_range(platform->pci_ecam_base, platform->pci_ecam_size, "PCI ECAM");
  }

  // Map PCI MMIO region explicitly (for device BARs)
  if (platform->pci_mmio_base != 0) {
    map_mmio_range(platform->pci_mmio_base, platform->pci_mmio_size, "PCI MMIO");
  }

  // Map all discovered RAM regions
  KDEBUG_LOG("Mapping %d RAM region(s):", platform->num_mem_regions);
  for (int r = 0; r < platform->num_mem_regions; r++) {
    uint64_t ram_base = platform->mem_regions[r].base;
    uint64_t ram_size = platform->mem_regions[r].size;

    KDEBUG_LOG("  Region %d: 0x%llx - 0x%llx (%llu MB)", r,
               (unsigned long long)ram_base,
               (unsigned long long)(ram_base + ram_size),
               (unsigned long long)(ram_size / 1024 / 1024));

    // Calculate which L2 entries this region spans
    // Each L2 entry covers 1 GB (30 bits), use bits [38:30] of address
    uint64_t start_l2_idx = (ram_base >> 30) & 0x1FF;
    uint64_t end_addr = ram_base + ram_size - 1;
    uint64_t end_l2_idx = (end_addr >> 30) & 0x1FF;

    // Map each 1 GB chunk that this region spans
    for (uint64_t l2_idx = start_l2_idx; l2_idx <= end_l2_idx; l2_idx++) {
      // Setup L2 entry to point to pt_l1[l2_idx] if not already set
      if (pt_l2[l2_idx] == 0) {
        pt_l2[l2_idx] = ((uint64_t)pt_l1[l2_idx] >> 12) << 10 | PTE_TABLE;
      }

      // Calculate the portion of memory that falls in this 1 GB slot
      uint64_t slot_start = l2_idx << 30;              // Start of this 1 GB
      uint64_t slot_end = slot_start + 0x40000000 - 1; // End of this 1 GB

      // Find overlap between region and this slot
      uint64_t map_start = (ram_base > slot_start) ? ram_base : slot_start;
      uint64_t map_end = (end_addr < slot_end) ? end_addr : slot_end;

      // Calculate L1 entry range for this portion
      uint64_t start_l1_idx =
          (map_start & 0x3FFFFFFF) / 0x200000; // Within 1 GB
      uint64_t end_l1_idx = (map_end & 0x3FFFFFFF) / 0x200000;

      // Populate L1 entries with 2 MB megapages
      for (uint64_t l1_idx = start_l1_idx; l1_idx <= end_l1_idx; l1_idx++) {
        uint64_t phys = (l2_idx << 30) + (l1_idx * 0x200000);
        pt_l1[l2_idx][l1_idx] = (phys >> 12) << 10 | PTE_RAM;
      }
    }
  }

  // Build satp value
  uint64_t pt_root_ppn = (uint64_t)pt_l2 >> 12;
  satp = SATP_MODE_SV39 | pt_root_ppn;

  KDEBUG_LOG("Enabling MMU (satp=0x%llx)...", (unsigned long long)satp);

  // Write satp and flush TLB
  __asm__ volatile("csrw satp, %0" : : "r"(satp));
  __asm__ volatile("sfence.vma");

  // Verify MMU is enabled
  __asm__ volatile("csrr %0, satp" : "=r"(satp));
  mode = satp >> 60;
  KASSERT(mode != 0, "Failed to enable MMU");

  KDEBUG_LOG("MMU enabled successfully");
}

// Build free memory region list
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

  // Reserve kernel region (_text_start to _end)
  // This includes .text, .rodata, .data, and .bss sections
  // Note: Stack and page tables are in .bss, so they're already included
  uintptr_t kernel_base = (uintptr_t)_text_start;
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
// Platform Initialization
// =============================================================================

static void wfi_timer_callback(void) {}

// Platform-specific initialization
void platform_init(platform_t *platform, void *fdt, void *kernel) {
  KLOG("rv64 init...");

  memset(platform, 0, sizeof(platform_t));
  platform->kernel = kernel;

  // Initialize IRQ ring buffer
  kirq_ring_init(&platform->irq_ring);

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
  timer_init(platform, fdt);
  KLOG("timer init ok");

  KLOG("virtio scan...");
  pci_scan_devices(platform);
  mmio_scan_devices(platform);
  KLOG("virtio scan ok");

  KLOG("rv64 init ok");
}

// Wait for interrupt with timeout
// timeout_ns: timeout in nanoseconds (UINT64_MAX = wait forever)
// Returns: current time in nanoseconds
ktime_t platform_wfi(platform_t *platform, ktime_t timeout_ns) {
  if (timeout_ns == 0) {
    return timer_get_current_time_ns(platform);
  }

  // Disable interrupts to check condition atomically (clear SIE bit in sstatus)
  __asm__ volatile("csrci sstatus, 0x2" ::: "memory");

  // Check if an interrupt has already fired (ring buffer not empty)
  if (!kirq_ring_is_empty(&platform->irq_ring)) {
    __asm__ volatile("csrsi sstatus, 0x2" ::: "memory"); // Re-enable IRQs
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
  __asm__ volatile("csrsi sstatus, 0x2" ::: "memory");

  // Cancel timer if it was set
  if (timeout_ns != UINT64_MAX) {
    timer_cancel(platform);
  }

  // Return current time
  return timer_get_current_time_ns(platform);
}

// Abort system execution (shutdown/halt)
void platform_abort(void) {
  // Disable interrupts (clear SIE bit in sstatus)
  __asm__ volatile("csrci sstatus, 0x2" ::: "memory");
  // Request shutdown via SBI (causes QEMU to exit cleanly)
  sbi_shutdown();
  // Should never reach here, but halt just in case
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

// Configure PCI device interrupts (RISC-V 64: legacy INTx)
// Returns hardware IRQ number to use for interrupt registration
int platform_pci_setup_interrupts(platform_t *platform, uint8_t bus,
                                  uint8_t slot, uint8_t func,
                                  void *transport) {
  (void)bus;       // Unused on RISC-V
  (void)transport; // Not needed for INTx

  // Read PCI interrupt pin from config space
  uint8_t irq_pin = platform_pci_config_read8(platform, bus, slot, func,
                                              PCI_REG_INTERRUPT_PIN);

  // Calculate hardware IRQ number using platform swizzling
  uint32_t irq_num = platform_pci_irq_swizzle(platform, slot, irq_pin);

  return (int)irq_num;
}

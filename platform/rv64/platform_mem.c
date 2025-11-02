// RISC-V 64-bit Memory Management
// MMU setup, memory discovery, and free region tracking

#include "kbase.h"
#include "kconfig.h"
#include "platform.h"
#include "printk.h"
#include <stddef.h>
#include <stdint.h>

// Forward declaration
int platform_mem_init(platform_t *platform, void *fdt);

// Linker symbols (provided by linker.ld)
extern uint8_t _text_start[];
extern uint8_t _text_end[];
extern uint8_t _rodata_start[];
extern uint8_t _rodata_end[];
extern uint8_t _data_start[];
extern uint8_t _data_end[];
extern uint8_t _bss_start[];
extern uint8_t _bss_end[];
extern uint8_t _end[];
extern uint8_t stack_bottom[];
extern uint8_t stack_top[];

// ============================================================================
// Sv39 Page Table Configuration
// ============================================================================

// Sv39 uses 3-level page tables with 4KB pages
// L2 (root): 512 entries, each covers 1 GB
// L1: 512 entries, each covers 2 MB
// L0 (leaf): 512 entries, each covers 4 KB

// Page table entry flags
#define PTE_V (1UL << 0)  // Valid
#define PTE_R (1UL << 1)  // Readable
#define PTE_W (1UL << 2)  // Writable
#define PTE_X (1UL << 3)  // Executable
#define PTE_A (1UL << 6)  // Accessed
#define PTE_D (1UL << 7)  // Dirty

// Page table pointer (non-leaf): V=1, R=W=X=0
#define PTE_TABLE (PTE_V)

// Normal RAM: readable, writable, executable, accessed, dirty
#define PTE_RAM (PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D)

// Device MMIO: readable, writable, accessed, dirty (no execute)
#define PTE_MMIO (PTE_V | PTE_R | PTE_W | PTE_A | PTE_D)

// satp register fields (for Sv39)
#define SATP_MODE_SV39 (8UL << 60)  // Mode = Sv39

// Page tables (static allocation in BSS)
// Using 2 MB megapages, we need:
// - 1 L2 table (root)
// - 1 L1 table for RAM region (0x80000000 - 0x88000000 = 128 MB)
// - 1 L1 table for low memory MMIO (0x00000000 - 0x40000000)
static uint64_t pt_l2[512] __attribute__((aligned(4096)));
static uint64_t pt_l1_ram[512] __attribute__((aligned(4096)));
static uint64_t pt_l1_mmio[512] __attribute__((aligned(4096)));

// ============================================================================
// MMU Configuration
// ============================================================================

// Check if MMU is already enabled
static int mmu_is_enabled(void) {
  uint64_t satp;
  __asm__ volatile("csrr %0, satp" : "=r"(satp));
  uint64_t mode = satp >> 60;
  return (mode != 0);
}

// Enable MMU with Sv39 paging
// Returns: 0 on success, -1 on error
static int mmu_enable_sv39(void) {
  // Check if already enabled
  if (mmu_is_enabled()) {
    printk("MMU already enabled, skipping setup\n");
    return 0;
  }

  printk("Setting up Sv39 page tables...\n");

  // Zero out page tables
  for (int i = 0; i < 512; i++) {
    pt_l2[i] = 0;
    pt_l1_ram[i] = 0;
    pt_l1_mmio[i] = 0;
  }

  // Map RAM region: 0x80000000 - 0x88000000 (128 MB) using 2 MB megapages
  // This is GB 2 (entries start at index 2 in L2)
  uint64_t ram_base = 0x80000000;
  uint64_t ram_size = 0x08000000;  // 128 MB (typical QEMU default)

  // Calculate number of 2 MB pages needed
  uint64_t num_pages = (ram_size + 0x1FFFFF) / 0x200000;  // Round up

  for (uint64_t i = 0; i < num_pages && i < 512; i++) {
    uint64_t phys = ram_base + (i * 0x200000);
    // Create leaf entry in L1 table (R/W/X set = megapage)
    pt_l1_ram[i] = (phys >> 12) << 10 | PTE_RAM;
  }

  // L2[2] points to L1_ram (covers 0x80000000 - 0xBFFFFFFF = 1 GB)
  pt_l2[2] = ((uint64_t)pt_l1_ram >> 12) << 10 | PTE_TABLE;

  // Map low memory MMIO devices (0x00000000 - 0x40000000)
  // This includes: DTB, UART, VirtIO MMIO, etc.
  // Using 2 MB megapages for simplicity
  uint64_t mmio_base = 0x00000000;
  uint64_t mmio_size = 0x40000000;  // 1 GB
  num_pages = mmio_size / 0x200000;  // 512 pages of 2 MB

  for (uint64_t i = 0; i < num_pages && i < 512; i++) {
    uint64_t phys = mmio_base + (i * 0x200000);
    // Create leaf entry in L1 table (R/W set, no X = MMIO)
    pt_l1_mmio[i] = (phys >> 12) << 10 | PTE_MMIO;
  }

  // L2[0] points to L1_mmio (covers 0x00000000 - 0x3FFFFFFF = 1 GB)
  pt_l2[0] = ((uint64_t)pt_l1_mmio >> 12) << 10 | PTE_TABLE;

  // Build satp value
  uint64_t pt_root_ppn = (uint64_t)pt_l2 >> 12;
  uint64_t satp = SATP_MODE_SV39 | pt_root_ppn;

  printk("Enabling MMU (satp=0x");
  printk_hex64(satp);
  printk(")...\n");

  // Write satp and flush TLB
  __asm__ volatile("csrw satp, %0" : : "r"(satp));
  __asm__ volatile("sfence.vma");

  // Verify MMU is enabled
  if (!mmu_is_enabled()) {
    printk("ERROR: Failed to enable MMU\n");
    return -1;
  }

  printk("MMU enabled successfully\n");
  return 0;
}

// ============================================================================
// Reserved Region Tracking
// ============================================================================

typedef struct {
  uintptr_t base;
  size_t size;
  const char *name;
} reserved_region_t;

// Calculate all reserved regions
// Returns: number of reserved regions
static int calculate_reserved_regions(void *fdt, reserved_region_t *regions,
                                      int max_regions) {
  (void)max_regions;  // Reserved for future bounds checking
  int count = 0;

  // DTB region (from FDT pointer and size from header)
  if (fdt) {
    struct fdt_header *header = (struct fdt_header *)fdt;
    uint32_t totalsize = kbe32toh(header->totalsize);

    regions[count].base = (uintptr_t)fdt;
    regions[count].size = totalsize;
    regions[count].name = "DTB";
    count++;
  }

  // Kernel region (from linker symbols)
  uintptr_t kernel_start = (uintptr_t)_text_start;
  uintptr_t kernel_end = (uintptr_t)_end;

  regions[count].base = kernel_start;
  regions[count].size = kernel_end - kernel_start;
  regions[count].name = "Kernel";
  count++;

  // Stack region (from linker symbols)
  uintptr_t stack_start = (uintptr_t)stack_bottom;
  uintptr_t stack_end = (uintptr_t)stack_top;

  regions[count].base = stack_start;
  regions[count].size = stack_end - stack_start;
  regions[count].name = "Stack";
  count++;

  // Page tables region (static arrays in BSS)
  uintptr_t pt_start = (uintptr_t)pt_l2;
  uintptr_t pt_end = (uintptr_t)pt_l1_mmio + sizeof(pt_l1_mmio);

  regions[count].base = pt_start;
  regions[count].size = pt_end - pt_start;
  regions[count].name = "Page tables";
  count++;

  return count;
}

// Subtract a reserved region from available regions
// This may split a region into two parts
static void subtract_reserved(mem_region_t *avail, int *num_avail,
                              uintptr_t res_base, size_t res_size) {
  uintptr_t res_end = res_base + res_size;

  // Check each available region for overlap
  for (int i = 0; i < *num_avail; i++) {
    uintptr_t avail_base = avail[i].base;
    uintptr_t avail_end = avail_base + avail[i].size;

    // No overlap, skip
    if (res_end <= avail_base || res_base >= avail_end) {
      continue;
    }

    // Reserved region completely covers available region, remove it
    if (res_base <= avail_base && res_end >= avail_end) {
      // Remove this region by shifting remaining regions down
      for (int j = i; j < *num_avail - 1; j++) {
        avail[j] = avail[j + 1];
      }
      (*num_avail)--;
      i--;  // Recheck this index
      continue;
    }

    // Reserved region starts before available region and overlaps
    if (res_base <= avail_base && res_end > avail_base) {
      avail[i].base = res_end;
      avail[i].size = avail_end - res_end;
      continue;
    }

    // Reserved region ends after available region and overlaps
    if (res_base < avail_end && res_end >= avail_end) {
      avail[i].size = res_base - avail_base;
      continue;
    }

    // Reserved region is in the middle, split into two regions
    if (res_base > avail_base && res_end < avail_end) {
      // Keep first part in current slot
      avail[i].size = res_base - avail_base;

      // Add second part as new region (if space available)
      if (*num_avail < KCONFIG_MAX_MEM_REGIONS) {
        avail[*num_avail].base = res_end;
        avail[*num_avail].size = avail_end - res_end;
        (*num_avail)++;
      }
      continue;
    }
  }
}

// ============================================================================
// Memory Discovery and Initialization
// ============================================================================

// Initialize memory subsystem
// Discovers memory from FDT, sets up MMU, builds free region list
// Returns: 0 on success, negative on error
int platform_mem_init(platform_t *platform, void *fdt) {
  if (!platform || !fdt) {
    return -1;
  }

  printk("\n=== Memory Management Initialization ===\n");

  // Parse FDT boot context and populate platform->mem_regions
  int ret = platform_boot_context_parse(platform, fdt);
  if (ret != 0) {
    printk("ERROR: Failed to parse boot context\n");
    return ret;
  }

  // Set up MMU with Sv39 page tables
  ret = mmu_enable_sv39();
  if (ret != 0) {
    printk("WARNING: MMU setup failed, continuing without MMU\n");
    // Continue anyway - not critical for functionality
  }

  // Calculate reserved regions
  reserved_region_t reserved[16];
  int num_reserved = calculate_reserved_regions(fdt, reserved, 16);

  printk("\nReserved regions:\n");
  for (int i = 0; i < num_reserved; i++) {
    printk("  ");
    printk(reserved[i].name);
    printk(": base=0x");
    printk_hex64(reserved[i].base);
    printk(" size=0x");
    printk_hex64(reserved[i].size);
    printk(" (");
    printk_dec(reserved[i].size / 1024);
    printk(" KB)\n");
  }

  // Build free region list
  // platform->mem_regions already contains all memory regions from FDT
  // (populated by platform_boot_context_parse)
  // Now subtract each reserved region
  for (int i = 0; i < num_reserved; i++) {
    subtract_reserved(platform->mem_regions, &platform->num_mem_regions,
                     reserved[i].base, reserved[i].size);
  }

  // Print final free regions
  printk("\nAvailable memory regions:\n");
  uint64_t total_free = 0;
  for (int i = 0; i < platform->num_mem_regions; i++) {
    printk("  Region ");
    printk_dec(i);
    printk(": base=0x");
    printk_hex64(platform->mem_regions[i].base);
    printk(" size=0x");
    printk_hex64(platform->mem_regions[i].size);
    printk(" (");
    printk_dec(platform->mem_regions[i].size / (1024 * 1024));
    printk(" MB)\n");
    total_free += platform->mem_regions[i].size;
  }

  printk("Total available memory: ");
  printk_dec(total_free / (1024 * 1024));
  printk(" MB\n");

  printk("=== Memory Management Initialization Complete ===\n\n");

  return 0;
}

// ============================================================================
// Platform Memory API
// ============================================================================

// Get list of available (free) memory regions
mem_region_list_t platform_mem_regions(platform_t *platform) {
  mem_region_list_t list;
  list.regions = platform->mem_regions;
  list.count = platform->num_mem_regions;
  return list;
}

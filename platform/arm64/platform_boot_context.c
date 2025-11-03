// ARM64 Boot Context Parsing
// Parse FDT (Flattened Device Tree) and populate platform_t

#include "kbase.h"
#include "kconfig.h"
#include "platform.h"
#include "printk.h"

// Helper: Compare strings
static int str_equal(const char *a, const char *b) {
  while (*a && *b) {
    if (*a != *b)
      return 0;
    a++;
    b++;
  }
  return *a == *b;
}

// Parse FDT boot context and populate platform with memory regions
// Returns: 0 on success, negative on error
void platform_boot_context_parse(platform_t *platform, void *boot_context) {
  void *fdt = boot_context;

  printk("platform_boot_context_parse: ARM64 FDT parsing\n");

  KASSERT(fdt, "FDT is NULL");

  printk("platform_boot_context_parse: reading header\n");
  struct fdt_header *header = (struct fdt_header *)fdt;

  platform->fdt_base = (uintptr_t)fdt;
  platform->fdt_size = kbe32toh(header->totalsize);
  KLOG("FDT size: %u bytes", platform->fdt_size);
  // Align size up to 64KB page boundary
  platform->fdt_size = KALIGN(platform->fdt_size, ARM64_PAGE_SIZE);

  // Verify magic number
  uint32_t magic = kbe32toh(header->magic);
  KASSERT(magic == FDT_MAGIC, "Invalid FDT magic");

  // Initialize platform memory regions
  platform->num_mem_regions = 0;

  // Initialize device addresses to zero
  platform->gic_dist_base = 0;
  platform->gic_cpu_base = 0;
  platform->pci_ecam_base = 0;
  platform->pci_ecam_size = 0;
  platform->pci_mmio_base = 0;
  platform->pci_mmio_size = 0;

  // Stack-local storage for UART address (used for debug logging only)
  uintptr_t uart_base = 0;

  printk("platform_boot_context_parse: getting offsets\n");

  uint32_t off_struct = kbe32toh(header->off_dt_struct);
  uint32_t off_strings = kbe32toh(header->off_dt_strings);
  uint32_t size_struct = kbe32toh(header->size_dt_struct);

  const uint8_t *struct_start = (const uint8_t *)fdt + off_struct;
  const uint8_t *struct_end = struct_start + size_struct;
  const char *strings = (const char *)fdt + off_strings;

  const uint8_t *p = struct_start;

  // Track current node type
  int in_memory_node = 0;
  int in_uart_node = 0;
  int in_gic_node = 0;
  int in_pci_node = 0;

  // Temporary storage for current node
  uint64_t current_reg_addr = 0;
  uint64_t current_reg_size = 0;
  uint64_t current_reg_addr2 = 0; // For GIC CPU interface

  printk("platform_boot_context_parse: starting parse loop\n");

  int loop_count = 0;
  const int MAX_LOOPS = 10000;

  while (p < struct_end && loop_count < MAX_LOOPS) {
    loop_count++;
    const uint32_t *ptr = KALIGN_CAST(const uint32_t *, p);
    uint32_t token = kbe32toh(*ptr++);

    if (token == FDT_BEGIN_NODE) {
      // Reset state for new node
      in_memory_node = 0;
      in_uart_node = 0;
      in_gic_node = 0;
      in_pci_node = 0;
      current_reg_addr = 0;
      current_reg_size = 0;
      current_reg_addr2 = 0;

      // Check node name for memory@ prefix
      const char *name = KALIGN_CAST(const char *, ptr);
      if (name[0] == 'm' && name[1] == 'e' && name[2] == 'm' &&
          name[3] == 'o' && name[4] == 'r' && name[5] == 'y' &&
          name[6] == '@') {
        in_memory_node = 1;
      }

      // Skip node name
      while (*name)
        name++;
      name++;
      ptr = KALIGN_CAST(const uint32_t *,
                        (const uint8_t *)(((uintptr_t)name + 3) & ~3));

    } else if (token == FDT_PROP) {
      uint32_t len = kbe32toh(*ptr++);
      uint32_t nameoff = kbe32toh(*ptr++);
      const volatile uint8_t *value = (const volatile uint8_t *)ptr;
      const char *prop_name = strings + nameoff;

      // Check compatible strings
      if (str_equal(prop_name, "compatible") && len > 0) {
        const char *compat = (const char *)value;

        // Check for UART (pl011 or similar)
        if (str_equal(compat, "arm,pl011") ||
            str_equal(compat, "arm,primecell")) {
          in_uart_node = 1;
        }

        // Check for GIC (various versions)
        if (str_equal(compat, "arm,gic-400") ||
            str_equal(compat, "arm,cortex-a15-gic") ||
            str_equal(compat, "arm,cortex-a9-gic") ||
            str_equal(compat, "arm,gic-v2")) {
          in_gic_node = 1;
        }

        // Check for PCI ECAM
        if (str_equal(compat, "pci-host-ecam-generic")) {
          in_pci_node = 1;
        }
      }

      // Parse PCI ranges property to get MMIO region for BAR allocation
      if (in_pci_node && str_equal(prop_name, "ranges") && len >= 28) {
        // PCI ranges format: (child-addr, parent-addr, size) tuples
        // Each tuple is 7 cells (28 bytes):
        //   3 cells (12 bytes): child address (flags + addr)
        //   2 cells (8 bytes):  parent address
        //   2 cells (8 bytes):  size
        // We want 64-bit MMIO space (flags = 0x03000000 or 0x02000000)
        for (uint32_t offset = 0; offset + 28 <= len; offset += 28) {
          const volatile uint8_t *entry = value + offset;

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

      // Collect reg property
      if (str_equal(prop_name, "reg") && len >= 16) {
        current_reg_addr = kload_be64(value);
        current_reg_size = kload_be64(value + 8);

        // For GIC, we need the second register pair (CPU interface)
        if (len >= 32) {
          current_reg_addr2 = kload_be64(value + 16);
        }
      }

      ptr = KALIGN_CAST(const uint32_t *,
                        (const uint8_t *)(((uintptr_t)value + len + 3) & ~3));

    } else if (token == FDT_END_NODE) {
      // Save collected info based on node type
      if (in_memory_node && current_reg_addr != 0 &&
          platform->num_mem_regions < KCONFIG_MAX_MEM_REGIONS) {
        platform->mem_regions[platform->num_mem_regions].base =
            current_reg_addr;
        platform->mem_regions[platform->num_mem_regions].size =
            current_reg_size;
        platform->num_mem_regions++;
      }

      if (in_uart_node && current_reg_addr != 0) {
        uart_base = current_reg_addr;
      }

      if (in_gic_node && current_reg_addr != 0) {
        platform->gic_dist_base = current_reg_addr;
        if (current_reg_addr2 != 0) {
          platform->gic_cpu_base = current_reg_addr2;
        }
      }

      if (in_pci_node && current_reg_addr != 0) {
        platform->pci_ecam_base = current_reg_addr;
        platform->pci_ecam_size = current_reg_size;
      }

    } else if (token == FDT_NOP) {
      // NOP token - just skip it
      p = KALIGN_CAST(const uint8_t *, ptr);
      continue;

    } else if (token == FDT_END) {
      break;
    }

    p = KALIGN_CAST(const uint8_t *, ptr);
  }

  if (loop_count >= MAX_LOOPS) {
    printk("platform_boot_context_parse: WARNING: loop limit reached\n");
  }

  printk("platform_boot_context_parse: parse complete (");
  printk_dec(loop_count);
  printk(" iterations)\n");

  // Print discovered information
  printk("Discovered from FDT:\n");
  printk("  RAM regions: ");
  printk_dec(platform->num_mem_regions);
  printk("\n");
  for (int i = 0; i < platform->num_mem_regions; i++) {
    printk("    Region ");
    printk_dec(i);
    printk(": 0x");
    printk_hex64(platform->mem_regions[i].base);
    printk(" - 0x");
    printk_hex64(platform->mem_regions[i].base + platform->mem_regions[i].size);
    printk(" (");
    printk_dec(platform->mem_regions[i].size / 1024 / 1024);
    printk(" MB)\n");
  }

  if (uart_base != 0) {
    printk("  UART: 0x");
    printk_hex64(uart_base);
    printk("\n");
  }

  if (platform->gic_dist_base != 0) {
    printk("  GIC Distributor: 0x");
    printk_hex64(platform->gic_dist_base);
    printk("\n");
  }

  if (platform->gic_cpu_base != 0) {
    printk("  GIC CPU Interface: 0x");
    printk_hex64(platform->gic_cpu_base);
    printk("\n");
  }

  if (platform->pci_ecam_base != 0) {
    printk("  PCI ECAM: 0x");
    printk_hex64(platform->pci_ecam_base);
    printk(" (size: 0x");
    printk_hex64(platform->pci_ecam_size);
    printk(")\n");
  }

  if (platform->pci_mmio_base != 0) {
    printk("  PCI MMIO: 0x");
    printk_hex64(platform->pci_mmio_base);
    printk(" (size: 0x");
    printk_hex64(platform->pci_mmio_size);
    printk(")\n");
  }
  platform->pci_next_bar_addr =
      platform->pci_mmio_base ? platform->pci_mmio_base : 0x10000000;

  printk("\n");
}

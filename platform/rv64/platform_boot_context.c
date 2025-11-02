// RISC-V 64-bit Boot Context Parsing
// Parse FDT (Flattened Device Tree) and populate platform_t

#include "kbase.h"
#include "kconfig.h"
#include "platform.h"
#include "printk.h"
#include <stdint.h>

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
int platform_boot_context_parse(platform_t *platform, void *boot_context) {
  void *fdt = boot_context;

  printk("platform_boot_context_parse: RISC-V 64-bit FDT parsing\n");

  if (!fdt || !platform) {
    printk("platform_boot_context_parse: NULL parameter\n");
    return -1;
  }

  printk("platform_boot_context_parse: reading header\n");
  struct fdt_header *header = (struct fdt_header *)fdt;

  // Verify magic number
  uint32_t magic = kbe32toh(header->magic);
  if (magic != FDT_MAGIC) {
    printk("Error: Invalid FDT magic: 0x");
    printk_hex32(magic);
    printk("\n");
    return -1;
  }

  // Initialize platform memory regions
  platform->num_mem_regions = 0;

  // Initialize device address fields
  platform->uart_base = 0;
  platform->plic_base = 0;
  platform->clint_base = 0;
  platform->pci_ecam_base = 0;
  platform->pci_ecam_size = 0;
  platform->pci_mmio_base = 0;
  platform->pci_mmio_size = 0;
  platform->virtio_mmio_base = 0;

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
  int in_plic_node = 0;
  int in_clint_node = 0;
  int in_pci_node = 0;
  int in_virtio_mmio_node = 0;

  // Temporary storage for current node
  uint64_t current_reg_addr = 0;
  uint64_t current_reg_size = 0;

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
      in_plic_node = 0;
      in_clint_node = 0;
      in_pci_node = 0;
      in_virtio_mmio_node = 0;
      current_reg_addr = 0;
      current_reg_size = 0;

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

        // Check for UART (various RISC-V UARTs)
        if (str_equal(compat, "ns16550a") || str_equal(compat, "ns16550") ||
            str_equal(compat, "sifive,uart0")) {
          in_uart_node = 1;
        }

        // Check for PLIC (Platform-Level Interrupt Controller)
        if (str_equal(compat, "riscv,plic0") ||
            str_equal(compat, "sifive,plic-1.0.0")) {
          in_plic_node = 1;
        }

        // Check for CLINT (Core-Local Interruptor)
        if (str_equal(compat, "riscv,clint0") ||
            str_equal(compat, "sifive,clint0")) {
          in_clint_node = 1;
        }

        // Check for PCI ECAM
        if (str_equal(compat, "pci-host-ecam-generic")) {
          in_pci_node = 1;
        }

        // Check for VirtIO MMIO
        if (str_equal(compat, "virtio,mmio")) {
          in_virtio_mmio_node = 1;
        }
      }

      // Parse PCI ranges property to get MMIO region for BAR allocation
      if (in_pci_node && str_equal(prop_name, "ranges") && len >= 28) {
        // PCI ranges format: (child-addr, parent-addr, size) tuples
        // Each tuple is 7 cells (28 bytes):
        //   3 cells (12 bytes): child address (flags + addr)
        //   2 cells (8 bytes):  parent address
        //   2 cells (8 bytes):  size
        // We want 64-bit MMIO space (flags = 0x03000000 or 0x42000000)
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
            break;  // Use the first MMIO range we find
          }
        }
      }

      // Collect reg property
      if (str_equal(prop_name, "reg") && len >= 16) {
        current_reg_addr = kload_be64(value);
        current_reg_size = kload_be64(value + 8);
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
        platform->uart_base = current_reg_addr;
      }

      if (in_plic_node && current_reg_addr != 0) {
        platform->plic_base = current_reg_addr;
      }

      if (in_clint_node && current_reg_addr != 0) {
        platform->clint_base = current_reg_addr;
      }

      if (in_pci_node && current_reg_addr != 0) {
        platform->pci_ecam_base = current_reg_addr;
        platform->pci_ecam_size = current_reg_size;
      }

      if (in_virtio_mmio_node && current_reg_addr != 0 &&
          platform->virtio_mmio_base == 0) {
        // Store the first VirtIO MMIO device base address
        platform->virtio_mmio_base = current_reg_addr;
      }

    } else if (token == FDT_NOP) {
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

  // Validate MMIO addresses are within fixed range (0x0 - 0x40000000)
  #define MMIO_RANGE_END 0x40000000ULL

  if (platform->uart_base != 0 && platform->uart_base >= MMIO_RANGE_END) {
    printk("ERROR: UART address 0x");
    printk_hex64(platform->uart_base);
    printk(" outside fixed MMIO range (0x0-0x");
    printk_hex64(MMIO_RANGE_END);
    printk(")\n");
    kpanic("UART address outside fixed MMIO range");
  }

  if (platform->plic_base != 0 && platform->plic_base >= MMIO_RANGE_END) {
    printk("ERROR: PLIC address 0x");
    printk_hex64(platform->plic_base);
    printk(" outside fixed MMIO range (0x0-0x");
    printk_hex64(MMIO_RANGE_END);
    printk(")\n");
    kpanic("PLIC address outside fixed MMIO range");
  }

  if (platform->clint_base != 0 && platform->clint_base >= MMIO_RANGE_END) {
    printk("ERROR: CLINT address 0x");
    printk_hex64(platform->clint_base);
    printk(" outside fixed MMIO range (0x0-0x");
    printk_hex64(MMIO_RANGE_END);
    printk(")\n");
    kpanic("CLINT address outside fixed MMIO range");
  }

  if (platform->pci_ecam_base != 0 && platform->pci_ecam_base >= MMIO_RANGE_END) {
    printk("ERROR: PCI ECAM address 0x");
    printk_hex64(platform->pci_ecam_base);
    printk(" outside fixed MMIO range (0x0-0x");
    printk_hex64(MMIO_RANGE_END);
    printk(")\n");
    kpanic("PCI ECAM address outside fixed MMIO range");
  }

  if (platform->virtio_mmio_base != 0 && platform->virtio_mmio_base >= MMIO_RANGE_END) {
    printk("ERROR: VirtIO MMIO address 0x");
    printk_hex64(platform->virtio_mmio_base);
    printk(" outside fixed MMIO range (0x0-0x");
    printk_hex64(MMIO_RANGE_END);
    printk(")\n");
    kpanic("VirtIO MMIO address outside fixed MMIO range");
  }

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

  if (platform->uart_base != 0) {
    printk("  UART: 0x");
    printk_hex64(platform->uart_base);
    printk("\n");
  }

  if (platform->plic_base != 0) {
    printk("  PLIC: 0x");
    printk_hex64(platform->plic_base);
    printk("\n");
  }

  if (platform->clint_base != 0) {
    printk("  CLINT: 0x");
    printk_hex64(platform->clint_base);
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

  if (platform->virtio_mmio_base != 0) {
    printk("  VirtIO MMIO: 0x");
    printk_hex64(platform->virtio_mmio_base);
    printk("\n");
  }

  printk("\n");

  return 0;
}

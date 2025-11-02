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

  // Stack-local storage for device addresses (used for debug logging only)
  uintptr_t uart_base = 0;
  uintptr_t plic_base = 0;
  uintptr_t clint_base = 0;
  uintptr_t pci_ecam_base = 0;
  size_t pci_ecam_size = 0;

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
      const uint8_t *value = (const uint8_t *)ptr;
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
      }

      // Collect reg property
      if (str_equal(prop_name, "reg") && len >= 16) {
        uint32_t addr_high =
            (value[0] << 24) | (value[1] << 16) | (value[2] << 8) | value[3];
        uint32_t addr_low =
            (value[4] << 24) | (value[5] << 16) | (value[6] << 8) | value[7];
        uint32_t size_high =
            (value[8] << 24) | (value[9] << 16) | (value[10] << 8) | value[11];
        uint32_t size_low = (value[12] << 24) | (value[13] << 16) |
                            (value[14] << 8) | value[15];

        current_reg_addr = ((uint64_t)addr_high << 32) | addr_low;
        current_reg_size = ((uint64_t)size_high << 32) | size_low;
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

      if (in_plic_node && current_reg_addr != 0) {
        plic_base = current_reg_addr;
      }

      if (in_clint_node && current_reg_addr != 0) {
        clint_base = current_reg_addr;
      }

      if (in_pci_node && current_reg_addr != 0) {
        pci_ecam_base = current_reg_addr;
        pci_ecam_size = current_reg_size;
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

  if (plic_base != 0) {
    printk("  PLIC: 0x");
    printk_hex64(plic_base);
    printk("\n");
  }

  if (clint_base != 0) {
    printk("  CLINT: 0x");
    printk_hex64(clint_base);
    printk("\n");
  }

  if (pci_ecam_base != 0) {
    printk("  PCI ECAM: 0x");
    printk_hex64(pci_ecam_base);
    printk(" (size: 0x");
    printk_hex64(pci_ecam_size);
    printk(")\n");
  }

  printk("\n");

  return 0;
}

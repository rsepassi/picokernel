// Device Tree parsing for ARM64
// Parses the FDT (Flattened Device Tree) passed by the bootloader

#include "kbase.h"
#include "platform.h"
#include "printk.h"
#include <stdint.h>

// Helper to print indentation
static void print_indent(int depth) {
  for (int i = 0; i < depth * 2; i++) {
    printk_putc(' ');
  }
}

// Helper to print property data as hex bytes
static void print_prop_data(const uint8_t *data, uint32_t len) {
  if (len == 0) {
    printk("<empty>");
    return;
  }

  // Check if it's a string (null-terminated printable ASCII)
  int is_string = 1;
  for (uint32_t i = 0; i < len - 1; i++) {
    if (data[i] == 0) {
      // Null in middle or multiple strings
      if (i < len - 2) {
        is_string = 0;
        break;
      }
    } else if (data[i] < 32 || data[i] > 126) {
      is_string = 0;
      break;
    }
  }
  if (is_string && data[len - 1] == 0) {
    printk_putc('"');
    printk((const char *)data);
    printk_putc('"');
    return;
  }

  // Try to print as cells (32-bit values) if length is multiple of 4
  if (len % 4 == 0 && len <= 16) {
    printk_putc('<');
    for (uint32_t i = 0; i < len; i += 4) {
      if (i > 0)
        printk_putc(' ');
      uint32_t cell = (data[i] << 24) | (data[i + 1] << 16) |
                      (data[i + 2] << 8) | data[i + 3];
      printk_hex32(cell);
    }
    printk_putc('>');
    return;
  }

  // Otherwise print as hex bytes
  printk_putc('[');
  for (uint32_t i = 0; i < len && i < 32; i++) {
    if (i > 0)
      printk_putc(' ');
    const char hex[] = "0123456789abcdef";
    printk_putc(hex[data[i] >> 4]);
    printk_putc(hex[data[i] & 0xf]);
  }
  if (len > 32) {
    printk("...");
  }
  printk_putc(']');
}

// Walk and print the device tree structure
static const uint8_t *fdt_walk_node(const uint8_t *p, const char *strings,
                                    int depth) {
  const uint32_t *ptr = KALIGN_CAST(const uint32_t *, p);
  uint32_t token = kbe32toh(*ptr++);

  if (token != FDT_BEGIN_NODE) {
    return (const uint8_t *)ptr;
  }

  // Get node name
  const char *name = (const char *)ptr;
  print_indent(depth);
  if (name[0] == '\0') {
    printk("/ {\n");
  } else {
    printk(name);
    printk(" {\n");
  }

  // Skip name (null-terminated, 4-byte aligned)
  const uint8_t *name_ptr = (const uint8_t *)ptr;
  while (*name_ptr != 0) {
    name_ptr++;
  }
  name_ptr++; // Skip the null terminator
  // Align to 4-byte boundary
  ptr = (uint32_t *)(((uint64_t)name_ptr + 3) & ~3);

  // Process properties and child nodes
  while (1) {
    token = kbe32toh(*ptr++);

    if (token == FDT_PROP) {
      uint32_t len = kbe32toh(*ptr++);
      uint32_t nameoff = kbe32toh(*ptr++);
      const uint8_t *value = (const uint8_t *)ptr;

      print_indent(depth + 1);
      printk(strings + nameoff);
      if (len > 0) {
        printk(" = ");
        print_prop_data(value, len);
      }
      printk(";\n");

      ptr = KALIGN_CAST(const uint32_t *,
                        (const uint8_t *)(((uint64_t)value + len + 3) & ~3));

    } else if (token == FDT_BEGIN_NODE) {
      // Process child node recursively
      ptr = KALIGN_CAST(uint32_t *, fdt_walk_node((const uint8_t *)ptr - 4,
                                                  strings, depth + 1));

    } else if (token == FDT_END_NODE) {
      print_indent(depth);
      printk("}\n");
      break;

    } else if (token == FDT_NOP) {
      continue;

    } else if (token == FDT_END) {
      break;

    } else {
      printk("Unknown token: ");
      printk_hex32(token);
      printk("\n");
      break;
    }
  }

  return (const uint8_t *)ptr;
}

// Scan memory for FDT magic number
static void *fdt_find(void) {
  // QEMU typically places DTB at end of RAM or specific locations
  // Try multiple ranges with different alignments

  printk("Scanning for device tree...\n");

  // First, try common high addresses (QEMU often places DTB near end of RAM)
  // 128 MB RAM = 0x08000000, so end is at 0x48000000
  uint64_t ranges[][3] = {
      {0x44000000, 0x48000000, 0x1000}, // Last 64 MB, 4KB align
      {0x40000000, 0x44000000, 0x1000}, // First 64 MB, 4KB align
      {0, 0, 0}};

  for (int r = 0; ranges[r][2] != 0; r++) {
    uint64_t start = ranges[r][0];
    uint64_t end = ranges[r][1];
    uint32_t align = ranges[r][2];

    for (uint64_t addr = start; addr < end; addr += align) {
      uint32_t *ptr = (uint32_t *)addr;
      uint32_t magic = kbe32toh(ptr[0]);

      if (magic == FDT_MAGIC) {
        printk("Found FDT at ");
        printk_hex64(addr);
        printk("\n");
        return (void *)addr;
      }
    }
  }

  printk("Device tree not found in memory scan\n");
  return 0;
}

// Main function to dump the device tree
void platform_fdt_dump(platform_t *platform, void *fdt) {
  (void)platform; // Unused parameter
  // If fdt is NULL, try to find it in memory
  if (!fdt) {
    printk("Warning: NULL device tree pointer, scanning memory...\n");
    fdt = fdt_find();
    if (!fdt) {
      printk("Error: Could not locate device tree\n");
      return;
    }
  }

  struct fdt_header *header = (struct fdt_header *)fdt;

  // Verify magic number
  uint32_t magic = kbe32toh(header->magic);
  if (magic != FDT_MAGIC) {
    printk("Error: Invalid FDT magic: ");
    printk_hex32(magic);
    printk("\n");
    return;
  }

  printk("\n=== Device Tree ===\n");
  printk("FDT at ");
  printk_hex64((uint64_t)fdt);
  printk("\n");

  uint32_t totalsize = kbe32toh(header->totalsize);
  uint32_t version = kbe32toh(header->version);

  printk("Total size: ");
  printk_dec(totalsize);
  printk(" bytes\n");

  printk("Version: ");
  printk_dec(version);
  printk("\n\n");

  // Get offsets
  uint32_t off_struct = kbe32toh(header->off_dt_struct);
  uint32_t off_strings = kbe32toh(header->off_dt_strings);

  // Walk the structure
  const uint8_t *struct_block = (const uint8_t *)fdt + off_struct;
  const char *strings = (const char *)fdt + off_strings;
  fdt_walk_node(struct_block, strings, 0);

  printk("\n=== End of Device Tree ===\n\n");
}

// Helper: Compare string with null-terminated string
static int str_equal(const char *a, const char *b) {
  while (*a && *b) {
    if (*a != *b)
      return 0;
    a++;
    b++;
  }
  return *a == *b;
}

// Simpler approach: iterate through all "virtio,mmio" compatible strings
// and extract their properties
static int fdt_count_virtio_devices(void *fdt, virtio_mmio_device_t *devices,
                                    int max_devices) {
  struct fdt_header *header = (struct fdt_header *)fdt;

  uint32_t off_struct = kbe32toh(header->off_dt_struct);
  uint32_t off_strings = kbe32toh(header->off_dt_strings);
  uint32_t size_struct = kbe32toh(header->size_dt_struct);

  const uint8_t *struct_start = (const uint8_t *)fdt + off_struct;
  const uint8_t *struct_end = struct_start + size_struct;
  const char *strings = (const char *)fdt + off_strings;

  int count = 0;
  uint64_t current_reg_addr = 0;
  uint64_t current_reg_size = 0;
  uint32_t current_irq = 0;
  int in_virtio_node = 0;

  const uint8_t *p = struct_start;

  while (p < struct_end && count < max_devices) {
    const uint32_t *ptr = KALIGN_CAST(const uint32_t *, p);
    uint32_t token = kbe32toh(*ptr++);

    if (token == FDT_BEGIN_NODE) {
      // Reset state for new node
      in_virtio_node = 0;
      current_reg_addr = 0;
      current_reg_size = 0;
      current_irq = 0;

      // Skip node name
      const char *name = KALIGN_CAST(const char *, ptr);
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

      // Check if this is a virtio,mmio node
      if (str_equal(prop_name, "compatible") && len >= 11) {
        if (str_equal((const char *)value, "virtio,mmio")) {
          in_virtio_node = 1;
        }
      }

      // Collect reg property (regardless of compatible - we'll check later)
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

      // Collect interrupts property (regardless of compatible - we'll check
      // later)
      if (str_equal(prop_name, "interrupts") && len >= 12) {
        uint32_t irq_type =
            (value[0] << 24) | (value[1] << 16) | (value[2] << 8) | value[3];
        uint32_t irq_num =
            (value[4] << 24) | (value[5] << 16) | (value[6] << 8) | value[7];

        if (irq_type == 0) { // SPI
          current_irq = 32 + irq_num;
        }
      }

      ptr = KALIGN_CAST(const uint32_t *,
                        (const uint8_t *)(((uintptr_t)value + len + 3) & ~3));

    } else if (token == FDT_END_NODE) {
      // If this was a virtio,mmio node and we collected required info, save it
      if (in_virtio_node && current_reg_addr != 0) {
        devices[count].base_addr = current_reg_addr;
        devices[count].size = current_reg_size;
        devices[count].irq = current_irq;
        count++;
      }
      // Reset for next node
      in_virtio_node = 0;

    } else if (token == FDT_NOP) {
      // Skip NOPs
      continue;

    } else if (token == FDT_END) {
      break;
    }

    p = KALIGN_CAST(const uint8_t *, ptr);
  }

  return count;
}

// Find VirtIO MMIO devices in device tree
int platform_fdt_find_virtio_mmio(platform_t *platform, void *fdt,
                                  virtio_mmio_device_t *devices,
                                  int max_devices) {
  (void)platform; // Platform not needed for FDT parsing
  if (!fdt || !devices || max_devices <= 0) {
    return 0;
  }

  struct fdt_header *header = (struct fdt_header *)fdt;

  // Verify magic number
  uint32_t magic = kbe32toh(header->magic);
  if (magic != FDT_MAGIC) {
    return 0;
  }

  return fdt_count_virtio_devices(fdt, devices, max_devices);
}

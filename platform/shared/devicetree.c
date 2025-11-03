// Device Tree parsing using libfdt
// Parses the FDT (Flattened Device Tree) passed by the bootloader

#include "kbase.h"

#ifdef KDEBUG
#include "mem_debug.h"
#include "platform.h"
#include "printk.h"
#include <libfdt/libfdt.h>
#include <stdint.h>

// Helper to print indentation
static void print_indent(int depth) {
  for (int i = 0; i < depth; i++) {
    printk("  ");
  }
}

// Helper to determine if a property value is printable as a string
static bool is_printable_string(const char *data, int len) {
  if (len == 0)
    return false;

  // Check if it's a valid null-terminated string
  if (data[len - 1] != '\0')
    return false;

  // Check if all characters are printable (or null terminators for string
  // lists)
  for (int i = 0; i < len - 1; i++) {
    if (data[i] == '\0') {
      // Allow null terminators in string lists
      continue;
    }
    if (data[i] < 0x20 || data[i] > 0x7E) {
      return false;
    }
  }

  return true;
}

// Helper to print property value
static void print_property_value(const void *data, int len) {
  if (len == 0) {
    printk("<empty>");
    return;
  }

  // Try to interpret as string(s)
  if (is_printable_string(data, len)) {
    const char *str = (const char *)data;
    printk("\"");
    int pos = 0;
    while (pos < len - 1) {
      if (pos > 0)
        printk("\", \"");
      printk("%s", str + pos);
      pos += str_len(str + pos) + 1;
    }
    printk("\"");
    return;
  }

  // Try to interpret as 32-bit cells
  if (len % 4 == 0) {
    const uint32_t *cells = (const uint32_t *)data;
    int ncells = len / 4;
    printk("<");
    for (int i = 0; i < ncells; i++) {
      if (i > 0)
        printk(" ");
      printk("0x%08x", fdt32_ld(&cells[i]));
    }
    printk(">");
    return;
  }

  // Fall back to byte array
  const uint8_t *bytes = (const uint8_t *)data;
  printk("[");
  for (int i = 0; i < len; i++) {
    if (i > 0)
      printk(" ");
    printk("%02x", bytes[i]);
    if (i >= 15 && len > 16) {
      printk(" ... (%d more bytes)", len - i - 1);
      break;
    }
  }
  printk("]");
}

// Recursively dump device tree nodes
static void dump_fdt_node(const void *fdt, int node, int depth) {
  int len;
  const char *name = fdt_get_name(fdt, node, &len);
  if (!name) {
    return;
  }

  print_indent(depth);
  if (name[0] == '\0') {
    printk("/"); // Root node
  } else {
    printk("%s {", name);
  }
  printk("\n");

  // Print all properties
  int prop_offset = fdt_first_property_offset(fdt, node);
  while (prop_offset >= 0) {
    const char *prop_name;
    const void *prop_data =
        fdt_getprop_by_offset(fdt, prop_offset, &prop_name, &len);

    if (prop_data && prop_name) {
      print_indent(depth + 1);
      printk("%s = ", prop_name);
      print_property_value(prop_data, len);
      printk(";\n");
    }

    prop_offset = fdt_next_property_offset(fdt, prop_offset);
  }

  // Recursively process child nodes
  int child;
  fdt_for_each_subnode(child, fdt, node) {
    dump_fdt_node(fdt, child, depth + 1);
  }

  print_indent(depth);
  printk("};\n");
  if (depth == 0)
    printk("\n");
}

// Main function to dump the device tree
void platform_fdt_dump(platform_t *platform, void *fdt) {
  (void)platform;

  if (!fdt) {
    printk("platform_fdt_dump: no FDT provided\n");
    return;
  }

  // Validate FDT header
  int err = fdt_check_header(fdt);
  if (err != 0) {
    printk("platform_fdt_dump: invalid FDT header: %s\n", fdt_strerror(err));
    return;
  }

  printk("=== Device Tree ===\n\n");

  // Dump memory reservations
  int num_rsv = fdt_num_mem_rsv(fdt);
  if (num_rsv > 0) {
    printk("Memory Reservations:\n");
    for (int i = 0; i < num_rsv; i++) {
      uint64_t addr, size;
      if (fdt_get_mem_rsv(fdt, i, &addr, &size) == 0) {
        printk("  [%d] addr=0x%016llx size=0x%016llx\n", i,
               (unsigned long long)addr, (unsigned long long)size);
      }
    }
    printk("\n");
  }

  // Dump the device tree structure
  dump_fdt_node(fdt, 0, 0);

  printk("=== End Device Tree ===\n\n");
}
#endif // KDEBUG

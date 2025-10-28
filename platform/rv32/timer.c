// RISC-V Timer Implementation
// Uses SBI timer interface for one-shot timers

#include "timer.h"
#include "platform.h"
#include "printk.h"
#include "sbi.h"
#include <stdint.h>

#define NULL ((void *)0)

// Simple 64-bit division by 1000 for 32-bit platforms
// Avoids need for compiler runtime library
static uint64_t div64_by_1000(uint64_t value) {
  // Split into high and low 32-bit parts
  uint32_t high = (uint32_t)(value >> 32);
  uint32_t low = (uint32_t)(value & 0xFFFFFFFF);

  if (high == 0) {
    // If value fits in 32 bits, use simple division
    return low / 1000;
  }

  // For larger values: value / 1000 = (high * 2^32 + low) / 1000
  // = high * (2^32 / 1000) + (high * (2^32 % 1000) + low) / 1000
  // = high * 4294967 + (high * 296 + low) / 1000

  // First term: high * 4294967
  uint64_t result = (uint64_t)high * 4294967UL;

  // Second term: (high * 296 + low) / 1000
  // Compute high * 296 + low carefully to avoid overflow
  uint32_t remainder_part = high % 1000;
  uint32_t combined = remainder_part * 296;

  // Now add low, checking for overflow
  if (combined > 0xFFFFFFFFU - low) {
    // Overflow case: split the addition
    uint32_t overflow_amount = (0xFFFFFFFFU - combined) + 1;
    uint32_t remaining = low - overflow_amount;
    result += 0xFFFFFFFFU / 1000;                   // Add quotient of overflow
    result += (combined % 1000 + remaining) / 1000; // Add remainder quotient
  } else {
    // No overflow: simple case
    uint32_t sum = combined + low;
    result += sum / 1000;
  }

  return result;
}

// Global timer state
static uint32_t g_timer_freq = 10000000; // Default: 10 MHz (typical for QEMU)
static timer_callback_t g_timer_callback = NULL;
static volatile int g_timer_fired = 0;

// Helper to find a property in the device tree
// Returns NULL if not found, pointer to property data otherwise
static const uint8_t *fdt_find_property(void *fdt, const char *node_path,
                                        const char *prop_name,
                                        uint32_t *out_len) {
  (void)node_path; // Unused - we search cpus node directly
  if (!fdt)
    return NULL;

  struct fdt_header *header = (struct fdt_header *)fdt;
  uint32_t magic = fdt32_to_cpu(header->magic);
  if (magic != FDT_MAGIC) {
    return NULL;
  }

  uint32_t off_struct = fdt32_to_cpu(header->off_dt_struct);
  uint32_t off_strings = fdt32_to_cpu(header->off_dt_strings);

  const uint8_t *struct_block = (const uint8_t *)fdt + off_struct;
  const char *strings = (const char *)fdt + off_strings;
  const uint32_t *ptr = (const uint32_t *)struct_block;

  // For simplicity, we'll just search the cpus node for timebase-frequency
  // This is good enough for RISC-V which puts it there
  int in_cpus_node = 0;

  while (1) {
    uint32_t token = fdt32_to_cpu(*ptr++);

    if (token == FDT_BEGIN_NODE) {
      const char *name = (const char *)ptr;

      // Check if this is the cpus node
      if (name[0] == 'c' && name[1] == 'p' && name[2] == 'u' &&
          name[3] == 's') {
        in_cpus_node = 1;
      }

      // Skip name (null-terminated, 4-byte aligned)
      const uint8_t *name_ptr = (const uint8_t *)ptr;
      while (*name_ptr != 0)
        name_ptr++;
      name_ptr++;
      ptr = (uint32_t *)(((uintptr_t)name_ptr + 3) & ~3);

    } else if (token == FDT_PROP) {
      uint32_t len = fdt32_to_cpu(*ptr++);
      uint32_t nameoff = fdt32_to_cpu(*ptr++);
      const uint8_t *value = (const uint8_t *)ptr;
      const char *pname = strings + nameoff;

      // Check if this is our property and we're in the right node
      if (in_cpus_node) {
        // Simple string comparison
        const char *search = prop_name;
        const char *current = pname;
        int match = 1;
        while (*search && *current) {
          if (*search != *current) {
            match = 0;
            break;
          }
          search++;
          current++;
        }
        if (match && *search == '\0' && *current == '\0') {
          if (out_len)
            *out_len = len;
          return value;
        }
      }

      ptr = (uint32_t *)(((uintptr_t)value + len + 3) & ~3);

    } else if (token == FDT_END_NODE) {
      in_cpus_node = 0;

    } else if (token == FDT_NOP) {
      continue;

    } else if (token == FDT_END) {
      break;
    }
  }

  return NULL;
}

// Initialize timer subsystem
void timer_init(void *fdt) {
  printk("Initializing RISC-V timer...\n");

  // Try to read timebase-frequency from device tree
  if (fdt) {
    uint32_t len;
    const uint8_t *prop =
        fdt_find_property(fdt, "/cpus", "timebase-frequency", &len);

    if (prop && len == 4) {
      // Read as big-endian 32-bit value
      g_timer_freq =
          (prop[0] << 24) | (prop[1] << 16) | (prop[2] << 8) | prop[3];
      printk("Timer frequency from DT: ");
      printk_dec(g_timer_freq);
      printk(" Hz\n");
    } else if (prop && len == 8) {
      // Read as big-endian 64-bit value, but only use low 32 bits
      // (timer frequencies are always < 4GHz in practice)
      g_timer_freq =
          (prop[4] << 24) | (prop[5] << 16) | (prop[6] << 8) | prop[7];
      printk("Timer frequency from DT: ");
      printk_dec(g_timer_freq);
      printk(" Hz\n");
    } else {
      printk("Warning: Could not read timebase-frequency, using default ");
      printk_dec(g_timer_freq);
      printk(" Hz\n");
    }
  } else {
    printk("Warning: No device tree, using default timer frequency ");
    printk_dec(g_timer_freq);
    printk(" Hz\n");
  }

  printk("Timer initialized\n");
}

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(uint32_t milliseconds, timer_callback_t callback) {
  if (!callback) {
    printk("timer_set_oneshot_ms: NULL callback\n");
    return;
  }

  g_timer_callback = callback;
  g_timer_fired = 0;

  // Calculate target time
  uint64_t current_time = sbi_get_time();
  // Multiply timer freq by milliseconds, then divide by 1000
  // For typical values (10MHz * 1000ms = 10M ticks), this won't overflow 64
  // bits
  uint64_t product = g_timer_freq * (uint64_t)milliseconds;
  uint64_t ticks = div64_by_1000(product);
  uint64_t target_time = current_time + ticks;

  // Set the timer via SBI
  sbi_set_timer(target_time);

  printk("Timer set for ");
  printk_dec(milliseconds);
  printk("ms (");
  printk_dec((uint32_t)ticks);
  printk(" ticks)\n");
}

// Timer interrupt handler
void timer_interrupt_handler(void) {
  // Disable timer interrupt by setting to max value
  sbi_set_timer(UINT64_MAX);

  // Call user callback if set
  if (g_timer_callback) {
    timer_callback_t cb = g_timer_callback;
    g_timer_callback = NULL;
    g_timer_fired = 1;
    cb();
  }
}

// Get timer frequency
uint64_t timer_get_frequency(void) { return (uint64_t)g_timer_freq; }

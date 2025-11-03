// RISC-V SBI Timer Driver
// Uses SBI timer interface for one-shot timers

#include "platform.h"
#include "platform_impl.h"
#include "timer.h"
#include "printk.h"
#include "sbi.h"
#include "libfdt/libfdt.h"
#include <stddef.h>
#include <stdint.h>

// Timer interrupt handler (called from interrupt.c's trap_handler)
// Platform pointer passed from interrupt.c which has it as module-local
void timer_handler(platform_t *platform) {
  if (!platform) {
    return;
  }

  // Disable timer interrupt by setting timer to max value
  sbi_set_timer(~0ULL);

  // Call the user's callback if set
  if (platform->timer_callback) {
    timer_callback_t cb = platform->timer_callback;
    platform->timer_callback = NULL; // Clear before calling
    cb();
  }
}

// Helper to read a 32-bit big-endian value from FDT
static uint32_t fdt_read_u32(const uint8_t *data) {
  return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

// Search FDT for timebase-frequency property
static uint64_t find_timebase_frequency(void *fdt) {
  if (!fdt) {
    return 0;
  }

  struct fdt_header *header = (struct fdt_header *)fdt;

  // Verify magic number
  uint32_t magic = kbe32toh(header->magic);
  if (magic != FDT_MAGIC) {
    return 0;
  }

  // Get offsets
  uint32_t off_struct = kbe32toh(header->off_dt_struct);
  uint32_t off_strings = kbe32toh(header->off_dt_strings);

  const uint8_t *struct_block = (const uint8_t *)fdt + off_struct;
  const char *strings = (const char *)fdt + off_strings;

  // Walk through the device tree looking for timebase-frequency
  // Note: alignment is guaranteed by FDT spec
  const uint32_t *ptr;
  __builtin_memcpy(&ptr, &struct_block, sizeof(ptr));
  int in_cpus = 0; // Track if we're in /cpus node

  while (1) {
    uint32_t token = kbe32toh(*ptr++);

    if (token == FDT_BEGIN_NODE) {
      // Get node name
      const char *name = (const char *)ptr;

      // Check if this is the cpus node
      if (name[0] == 'c' && name[1] == 'p' && name[2] == 'u' &&
          name[3] == 's' && name[4] == '\0') {
        in_cpus = 1;
      }

      // Skip name (null-terminated, 4-byte aligned)
      const uint8_t *name_ptr = (const uint8_t *)ptr;
      while (*name_ptr != 0) {
        name_ptr++;
      }
      name_ptr++; // Skip null terminator
      ptr = (uint32_t *)(((uint64_t)name_ptr + 3) & ~3);

    } else if (token == FDT_PROP) {
      uint32_t len = kbe32toh(*ptr++);
      uint32_t nameoff = kbe32toh(*ptr++);
      const uint8_t *value = (const uint8_t *)ptr;
      const char *prop_name = strings + nameoff;

      // Check for timebase-frequency property
      if (in_cpus) {
        int match = 1;
        const char *target = "timebase-frequency";
        for (int i = 0; target[i] != '\0'; i++) {
          if (prop_name[i] != target[i]) {
            match = 0;
            break;
          }
        }
        if (match && prop_name[18] == '\0') {
          // Found it! Read the value
          if (len == 4) {
            return fdt_read_u32(value);
          } else if (len == 8) {
            uint64_t high = fdt_read_u32(value);
            uint64_t low = fdt_read_u32(value + 4);
            return (high << 32) | low;
          }
        }
      }

      ptr = (uint32_t *)(((uint64_t)value + len + 3) & ~3);

    } else if (token == FDT_END_NODE) {
      in_cpus = 0; // Exiting cpus node

    } else if (token == FDT_NOP) {
      continue;

    } else if (token == FDT_END) {
      break;

    } else {
      break;
    }
  }

  return 0;
}

// Initialize timer
void timer_init(platform_t *platform, void *fdt) {
  // Default: 10 MHz (common in QEMU)
  platform->timebase_freq = 10000000;

  // Try to read timebase frequency from device tree
  uint64_t freq = find_timebase_frequency(fdt);

  if (freq > 0) {
    platform->timebase_freq = freq;
    printk("Timebase frequency: ");
    printk_dec((uint32_t)(freq / 1000000));
    printk(" MHz (from device tree)\n");
  } else {
    printk("Timebase frequency: ");
    printk_dec((uint32_t)(platform->timebase_freq / 1000000));
    printk(" MHz (default)\n");
  }

  // Capture start time for timer_get_current_time_ms()
  platform->timer_start = rdtime();

  // Initialize callback to NULL
  platform->timer_callback = NULL;

  // Disable any pending timer interrupts
  sbi_set_timer(~0ULL);
}

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(platform_t *platform, uint32_t milliseconds,
                          timer_callback_t callback) {
  if (callback == NULL) {
    printk("timer_set_oneshot_ms: NULL callback\n");
    return;
  }

  if (platform->timebase_freq == 0) {
    printk("timer_set_oneshot_ms: Timer not initialized\n");
    return;
  }

  platform->timer_callback = callback;

  // Calculate ticks needed
  // ticks = (milliseconds * frequency) / 1000
  uint64_t ticks = ((uint64_t)milliseconds * platform->timebase_freq) / 1000;

  // Ensure we have at least 1 tick
  if (ticks == 0) {
    ticks = 1;
  }

  // Get current time
  uint64_t now = rdtime();

  // Set timer to fire at now + ticks
  uint64_t target = now + ticks;
  sbi_set_timer(target);

  printk("Timer set for ");
  printk_dec(milliseconds);
  printk("ms (");
  printk_dec((uint32_t)ticks);
  printk(" ticks)\n");
}

// Get current timer frequency
uint64_t timer_get_frequency(platform_t *platform) {
  if (!platform) {
    return 0;
  }
  return platform->timebase_freq;
}

// Get current time in milliseconds
uint64_t timer_get_current_time_ms(platform_t *platform) {
  if (!platform) {
    return 0;
  }

  uint64_t counter_now = rdtime();
  uint64_t counter_elapsed = counter_now - platform->timer_start;

  if (platform->timebase_freq == 0) {
    return 0;
  }

  // Convert counter ticks to milliseconds
  // ms = (ticks * 1000) / freq_hz
  return (counter_elapsed * 1000) / platform->timebase_freq;
}

// Get current time in nanoseconds
ktime_t timer_get_current_time_ns(platform_t *platform) {
  if (!platform) {
    return 0;
  }

  uint64_t counter_now = rdtime();
  uint64_t counter_elapsed = counter_now - platform->timer_start;

  if (platform->timebase_freq == 0) {
    return 0;
  }

  // Convert counter ticks to nanoseconds
  // ns = (ticks * 1000000000) / freq_hz
  return (counter_elapsed * 1000000000ULL) / platform->timebase_freq;
}

// Cancel any pending timer
void timer_cancel(platform_t *platform) {
  (void)platform;
  sbi_set_timer(~0ULL); // Set to UINT64_MAX to disable
}

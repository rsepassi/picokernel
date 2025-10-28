// ARM64 Platform Initialization
// Sets up interrupts, timer, and device enumeration

#include "fdt.h"
#include "interrupt.h"
#include "kernel.h"
#include "platform.h"
#include "printk.h"
#include "timer.h"
#include "virtio_mmio.h"
#include <stddef.h>

// Forward declare internal device enumeration function
void platform_fdt_dump(void *fdt);

static void wfi_timer_callback(void) {}

// Platform-specific initialization
void platform_init(platform_t *platform, void *fdt) {
  platform->timer_freq_hz = 0;
  platform->virtio_rng_present = 0;

  printk("Initializing ARM64 platform...\n");

  // Initialize exception vectors and GIC
  interrupt_init();

  // Initialize ARM Generic Timer
  timer_init();

  // Read and store timer frequency
  platform->timer_freq_hz = timer_get_frequency();

  // NOTE: Interrupts NOT enabled yet - will be enabled in event loop
  // to avoid spurious interrupts during device enumeration

  // Parse and display device tree
  platform_fdt_dump(fdt);

  printk("\n");

  // Discover VirtIO-MMIO devices from device tree
  printk("Discovering VirtIO devices from device tree...\n");
  virtio_mmio_device_t virtio_devices[32];
  int num_devices = fdt_find_virtio_mmio(fdt, virtio_devices, 32);

  if (num_devices > 0) {
    printk("Found ");
    printk_dec(num_devices);
    printk(" VirtIO MMIO device(s) in device tree\n");

    // Try to initialize VirtIO-RNG from discovered devices
    for (int i = 0; i < num_devices; i++) {
      printk("Attempting to initialize VirtIO device at 0x");
      printk_hex64(virtio_devices[i].base_addr);
      printk(" (IRQ ");
      printk_dec(virtio_devices[i].irq);
      printk(")...\n");

      virtio_rng_mmio_setup(platform, virtio_devices[i].base_addr,
                            virtio_devices[i].size, virtio_devices[i].irq);

      if (platform->virtio_rng_present) {
        printk("VirtIO-RNG initialized at device ");
        printk_dec(i);
        printk("\n");
        break;
      }
    }

    if (!platform->virtio_rng_present) {
      printk("No VirtIO-RNG device found\n");
    }
  } else {
    printk("No VirtIO MMIO devices found in device tree\n");
  }

  printk("\nPlatform initialization complete.\n\n");
}

// kplatform_tick and platform_submit are now implemented in virtio_mmio.c

// Wait for interrupt with timeout
// timeout_ms: timeout in milliseconds (UINT64_MAX = wait forever)
// Returns: current time in milliseconds
uint64_t platform_wfi(platform_t *platform, uint64_t timeout_ms) {
  (void)platform;

  // Set timeout timer if not UINT64_MAX
  if (timeout_ms != UINT64_MAX) {
    // For timeouts > UINT32_MAX ms, cap at UINT32_MAX
    uint32_t timeout_ms_32 =
        (timeout_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)timeout_ms;
    timer_set_oneshot_ms(timeout_ms_32, wfi_timer_callback);
  }

  // Wait for interrupt
  __asm__ volatile("wfi");

  // Return current time
  return timer_get_current_time_ms();
}

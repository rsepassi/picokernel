// x32 Platform Hooks Implementation
// Architecture-specific operations for VirtIO abstraction layer

#include "interrupt.h"
#include "platform.h"
#include "platform_impl.h"
#include "printk.h"
#include <stddef.h>

// IRQ registration for devices
// On x64, IRQ numbers (0-23+) are IOAPIC IRQ lines that need to be
// mapped to interrupt vectors (32+). This function handles both:
// 1. Routing the IRQ line through IOAPIC to CPU vector (with correct trigger type)
// 2. Registering the handler for that vector
// All VirtIO devices (both PCI and MMIO) use level-triggered, active-high interrupts
int platform_irq_register(platform_t *platform, uint32_t irq_num,
                          void (*handler)(void *), void *context) {
  // Convert IRQ line to interrupt vector (vectors start at 32)
  uint32_t vector = 32 + irq_num;

  // Debug: print IRQ registration
  printk("[IRQ] Registering IRQ line ");
  printk_dec(irq_num);
  printk(" -> vector ");
  printk_dec(vector);
  printk(" (MMIO/level)\n");

  // Register handler for this vector
  platform->irq_table[vector].handler = handler;
  platform->irq_table[vector].context = context;

  // Route IRQ line through IOAPIC with level-triggered, active-high
  // VirtIO devices (both PCI and MMIO variants) use these settings
  irq_register_mmio(platform, (uint8_t)irq_num, handler, context);

  return 0;
}

// IRQ enable for MMIO devices
// Unmask the IRQ line in the IOAPIC
void platform_irq_enable(platform_t *platform, uint32_t irq_num) {
  irq_enable(platform, (uint8_t)irq_num);
}

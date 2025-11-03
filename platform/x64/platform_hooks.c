// x32 Platform Hooks Implementation
// Architecture-specific operations for VirtIO abstraction layer

#include "interrupt.h"
#include "platform.h"
#include "platform_impl.h"
#include "printk.h"
#include <stddef.h>

// IRQ registration for devices
// On x64, handles both MSI-X vectors and IOAPIC IRQ lines:
// - irq_num >= 128: Direct CPU vector (MSI-X), register handler directly
// - irq_num < 128: IOAPIC IRQ/GSI line, route through IOAPIC to vector (32+irq_num)
// All VirtIO devices (both PCI and MMIO) use level-triggered, active-high interrupts
int platform_irq_register(platform_t *platform, uint32_t irq_num,
                          void (*handler)(void *), void *context) {
  if (irq_num >= 128 && irq_num < 256) {
    // MSI-X CPU vector range (128-255) - register directly
    irq_register(platform, (uint8_t)irq_num, handler, context);
    return 0;
  }

  // Hardware IRQ/GSI line (0-127) - route through IOAPIC
  uint32_t vector = 32 + irq_num;

  // Register handler for this vector
  platform->irq_table[vector].handler = handler;
  platform->irq_table[vector].context = context;

  // Route IRQ line through IOAPIC with level-triggered, active-high
  // VirtIO devices (both PCI and MMIO variants) use these settings
  irq_register_mmio(platform, (uint8_t)irq_num, handler, context);

  return 0;
}

// IRQ enable for devices
// For MSI-X vectors (>= 128): No-op (already enabled when MSI-X is configured)
// For IOAPIC IRQ/GSI lines (< 128): Unmask the IRQ line in the IOAPIC
void platform_irq_enable(platform_t *platform, uint32_t irq_num) {
  if (irq_num >= 128 && irq_num < 256) {
    // MSI-X vector - already enabled, nothing to do
    return;
  }

  // Hardware IRQ/GSI line - enable in IOAPIC
  irq_enable(platform, (uint8_t)irq_num);
}

// x32 Platform Hooks Implementation
// Architecture-specific operations for VirtIO abstraction layer

#include "interrupt.h"
#include "platform.h"
#include "platform_impl.h"
#include "printk.h"
#include <stddef.h>

// IRQ registration for devices
// On x64, IRQ numbers (0-23) are IOAPIC IRQ lines that need to be
// mapped to interrupt vectors (32+). This function handles both:
// 1. Routing the IRQ line through IOAPIC to CPU vector (with correct trigger type)
// 2. Registering the handler for that vector
// PCI devices (IRQ >= 16) use level-triggered interrupts
// MMIO/ISA devices (IRQ < 16) use edge-triggered interrupts
int platform_irq_register(platform_t *platform, uint32_t irq_num,
                          void (*handler)(void *), void *context) {
  // Convert IRQ line to interrupt vector (vectors start at 32)
  uint32_t vector = 32 + irq_num;

  // Debug: print IRQ registration
  printk("[IRQ] Registering IRQ line ");
  printk_dec(irq_num);
  printk(" -> vector ");
  printk_dec(vector);
  printk(" (");
  printk(irq_num >= 16 ? "PCI/level" : "MMIO/edge");
  printk(")\n");

  // Register handler for this vector
  platform->irq_table[vector].handler = handler;
  platform->irq_table[vector].context = context;

  // Route IRQ line through IOAPIC to vector with appropriate trigger type
  // PCI devices (IRQ >= 16) need level-triggered interrupts for INTx
  // MMIO/ISA devices (IRQ < 16) use edge-triggered interrupts
  if (irq_num >= 16) {
    irq_register_pci(platform, (uint8_t)irq_num, handler, context);
  } else {
    irq_register_mmio(platform, (uint8_t)irq_num, handler, context);
  }
  return 0;
}

// IRQ enable for MMIO devices
// Unmask the IRQ line in the IOAPIC
void platform_irq_enable(platform_t *platform, uint32_t irq_num) {
  irq_enable(platform, (uint8_t)irq_num);
}

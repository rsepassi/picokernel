// RV32 Platform Hooks Implementation
// Architecture-specific operations for VirtIO abstraction layer

#include "interrupt.h"
#include "platform.h"
#include "sbi.h"
#include <stddef.h>
#include <stdint.h>

// IRQ registration
int platform_irq_register(uint32_t irq_num, void (*handler)(void *),
                          void *context) {
  irq_register(irq_num, handler, context);
  return 0;
}

// IRQ enable
void platform_irq_enable(uint32_t irq_num) { irq_enable(irq_num); }

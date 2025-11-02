// ARM64 Interrupt Handling
// Exception vector setup and GIC management

#pragma once

#include "platform.h"
#include <stdint.h>

// Initialize exception vectors and GIC
void interrupt_init(platform_t *platform);

// Enable interrupts (unmask DAIF)
// Declared in platform.h, implemented here
// void platform_interrupt_enable(platform_t *platform);

// Disable interrupts (mask DAIF)
// Declared in platform.h, implemented here
// void platform_interrupt_disable(platform_t *platform);

// Register IRQ handler
void irq_register(platform_t *platform, uint32_t irq_num,
                  void (*handler)(void *), void *context);

// Enable (unmask) a specific IRQ in the GIC
void irq_enable(platform_t *platform, uint32_t irq_num);

// Dispatch IRQ (called from exception handler)
void irq_dispatch(platform_t *platform, uint32_t irq_num);

// Dump GIC configuration for a specific IRQ (for debugging)
void irq_dump_config(platform_t *platform, uint32_t irq_num);

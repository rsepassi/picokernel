// ARM64 Interrupt Handling
// Exception vector setup and GIC management

#pragma once

#include <stdint.h>

// Initialize exception vectors and GIC
void interrupt_init(void);

// Enable interrupts (unmask DAIF)
void platform_interrupt_enable(void);

// Disable interrupts (mask DAIF)
void platform_interrupt_disable(void);

// Register IRQ handler
void irq_register(uint32_t irq_num, void (*handler)(void *), void *context);

// Enable (unmask) a specific IRQ in the GIC
void irq_enable(uint32_t irq_num);

// Dispatch IRQ (called from exception handler)
void irq_dispatch(uint32_t irq_num);

// Dump GIC configuration for a specific IRQ (for debugging)
void irq_dump_config(uint32_t irq_num);

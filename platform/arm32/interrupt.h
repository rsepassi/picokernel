// ARM32 Interrupt Handling
// GICv2 (Generic Interrupt Controller) setup and management

#pragma once

#include <stdint.h>

// Initialize GIC and exception vectors
void interrupt_init(void);

// Enable interrupts (clear I bit in CPSR)
void platform_interrupt_enable(void);

// Disable interrupts (set I bit in CPSR)
void platform_interrupt_disable(void);

// Register IRQ handler
void irq_register(uint32_t irq_num, void (*handler)(void *), void *context);

// Enable (unmask) a specific IRQ in the GIC
void irq_enable(uint32_t irq_num);

// Dispatch IRQ (called from exception handler)
void irq_dispatch(uint32_t irq_num);

// Common interrupt handler (called from assembly)
void interrupt_handler(uint32_t vector);

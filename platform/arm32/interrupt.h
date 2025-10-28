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

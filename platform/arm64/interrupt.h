// ARM64 Interrupt Handling
// Exception vector setup and GIC management

#pragma once

#include <stdint.h>

// Initialize exception vectors and GIC
void interrupt_init(void);

// Enable interrupts (unmask DAIF)
void interrupt_enable(void);

// Disable interrupts (mask DAIF)
void interrupt_disable(void);

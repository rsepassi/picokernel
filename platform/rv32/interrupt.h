// RISC-V Interrupt Handling
// Trap handler setup and interrupt management

#pragma once

#include <stdint.h>

// Initialize interrupt handling (set up trap vector)
void interrupt_init(void);

// Enable interrupts globally
void interrupt_enable(void);

// Disable interrupts globally
void interrupt_disable(void);

// Common trap handler (called from trap.S)
void trap_handler(void);

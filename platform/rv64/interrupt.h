// RISC-V Interrupt Handling
// Trap handler setup and interrupt control

#pragma once

#include <stdint.h>

// Initialize trap handler
void interrupt_init(void);

// Enable interrupts (set SIE bit in sstatus)
void interrupt_enable(void);

// Disable interrupts (clear SIE bit in sstatus)
void interrupt_disable(void);

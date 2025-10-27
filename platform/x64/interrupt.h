// x64 Interrupt Handling
// IDT (Interrupt Descriptor Table) setup and management

#pragma once

#include <stdint.h>

// Initialize IDT and enable interrupts
void interrupt_init(void);

// Enable interrupts (sti instruction)
void interrupt_enable(void);

// Disable interrupts (cli instruction)
void interrupt_disable(void);

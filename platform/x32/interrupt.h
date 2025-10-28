// x32 Interrupt Handling
// IDT (Interrupt Descriptor Table) setup and management

#pragma once

#include <stdint.h>

// Initialize IDT and enable interrupts
void interrupt_init(void);

// Enable interrupts (sti instruction)
void platform_interrupt_enable(void);

// Disable interrupts (cli instruction)
void platform_interrupt_disable(void);

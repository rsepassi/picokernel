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

// Register IRQ handler
void irq_register(uint8_t vector, void (*handler)(void*), void* context);

// Enable (unmask) a specific IRQ in the PIC
void irq_enable(uint8_t vector);

// Dispatch IRQ (called from assembly)
void irq_dispatch(uint8_t vector);

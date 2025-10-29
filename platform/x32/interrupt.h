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

// Register IRQ handler
void irq_register(uint8_t irq_num, void (*handler)(void *), void *context);

// Enable (unmask) a specific IRQ
void irq_enable(uint8_t irq_num);

// Dispatch IRQ (called from exception handler)
void irq_dispatch(uint8_t irq_num);

// Interrupt handler (called from ISR stubs)
void interrupt_handler(uint64_t vector);

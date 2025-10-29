// RISC-V Interrupt Handling
// Trap handler setup and interrupt management

#pragma once

#include <stdint.h>

// Initialize interrupt handling (set up trap vector)
void interrupt_init(void);

// Enable interrupts globally
void platform_interrupt_enable(void);

// Disable interrupts globally
void platform_interrupt_disable(void);

// Register IRQ handler
void irq_register(uint32_t irq_num, void (*handler)(void *), void *context);

// Enable (unmask) a specific IRQ
void irq_enable(uint32_t irq_num);

// Dispatch IRQ (called from trap handler)
void irq_dispatch(uint32_t irq_num);

// Common trap handler (called from trap.S)
void trap_handler(void);

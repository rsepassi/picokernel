// RISC-V Interrupt Handling
// Trap handler setup and interrupt control

#pragma once

#include <stdint.h>

// Initialize trap handler
void interrupt_init(void);

// Enable interrupts (set SIE bit in sstatus)
void platform_interrupt_enable(void);

// Disable interrupts (clear SIE bit in sstatus)
void platform_interrupt_disable(void);

// Register IRQ handler
void irq_register(uint32_t irq_num, void (*handler)(void *), void *context);

// Enable (unmask) a specific IRQ in the PLIC
void irq_enable(uint32_t irq_num);

// Dispatch IRQ (called from exception handler)
void irq_dispatch(uint32_t irq_num);

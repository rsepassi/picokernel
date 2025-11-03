// RISC-V Interrupt Handling
// Trap handler setup and interrupt control

#pragma once

#include <stdint.h>

// Forward declaration (complete type defined in platform_impl.h)
struct platform;
typedef struct platform platform_t;

// Initialize trap handler
void interrupt_init(platform_t *platform);

// Enable interrupts (set SIE bit in sstatus)
void platform_interrupt_enable(platform_t *platform);

// Disable interrupts (clear SIE bit in sstatus)
void platform_interrupt_disable(platform_t *platform);

// Register IRQ handler
void irq_register(platform_t *platform, uint32_t irq_num,
                  void (*handler)(void *), void *context);

// Enable (unmask) a specific IRQ in the PLIC
void irq_enable(platform_t *platform, uint32_t irq_num);

// Dispatch IRQ (called from exception handler)
void irq_dispatch(uint32_t irq_num);

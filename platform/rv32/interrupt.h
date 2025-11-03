// RISC-V Interrupt Handling
// Trap handler setup and interrupt management

#pragma once

#include <stdint.h>

// Forward declaration (complete type defined in platform_impl.h)
struct platform;
typedef struct platform platform_t;

// Initialize interrupt handling (set up trap vector)
void interrupt_init(platform_t *platform);

// Enable interrupts globally
void platform_interrupt_enable(platform_t *platform);

// Disable interrupts globally
void platform_interrupt_disable(platform_t *platform);

// Register IRQ handler
void irq_register(platform_t *platform, uint32_t irq_num,
                  void (*handler)(void *), void *context);

// Enable (unmask) a specific IRQ
void irq_enable(platform_t *platform, uint32_t irq_num);

// Dispatch IRQ (called from trap handler)
void irq_dispatch(platform_t *platform, uint32_t irq_num);

// Common trap handler (called from trap.S)
void trap_handler(void);

// x64 Interrupt Handling
// IDT (Interrupt Descriptor Table) setup and management

#pragma once

#include <stdint.h>

// Forward declaration and typedef for platform
struct platform_t;
typedef struct platform_t platform_t;

// Initialize IDT and enable interrupts
void interrupt_init(platform_t *platform);

// Enable interrupts (sti instruction)
void platform_interrupt_enable(platform_t *platform);

// Disable interrupts (cli instruction)
void platform_interrupt_disable(platform_t *platform);

// Register IRQ handler
void irq_register(platform_t *platform, uint8_t vector, void (*handler)(void *),
                  void *context);

// Register MMIO IRQ handler (edge-triggered)
void irq_register_mmio(platform_t *platform, uint8_t vector,
                       void (*handler)(void *), void *context);

// Enable (unmask) a specific IRQ in the PIC
void irq_enable(platform_t *platform, uint8_t vector);

// Dispatch IRQ (called from assembly)
void irq_dispatch(uint8_t vector);

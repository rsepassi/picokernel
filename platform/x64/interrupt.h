// x32 Interrupt Handling
// IDT (Interrupt Descriptor Table) setup and management

#pragma once

#include <stdint.h>

// Forward declaration
typedef struct platform platform_t;

// Initialize IDT and enable interrupts
void interrupt_init(platform_t *platform);

// Enable interrupts (sti instruction)
void platform_interrupt_enable(platform_t *platform);

// Disable interrupts (cli instruction)
void platform_interrupt_disable(platform_t *platform);

// Register IRQ handler (for MSI-X devices)
void irq_register(platform_t *platform, uint8_t irq_num,
                  void (*handler)(void *), void *context);

// Register MMIO IRQ handler (edge-triggered, routes through IOAPIC)
void irq_register_mmio(platform_t *platform, uint8_t irq_num,
                       void (*handler)(void *), void *context);

// Register PCI IRQ handler (level-triggered, routes through IOAPIC)
void irq_register_pci(platform_t *platform, uint8_t irq_num,
                      void (*handler)(void *), void *context);

// Enable (unmask) a specific IRQ
void irq_enable(platform_t *platform, uint8_t irq_num);

// Dispatch IRQ (called from exception handler)
void irq_dispatch(uint8_t irq_num);

// Interrupt handler (called from ISR stubs)
void interrupt_handler(uint64_t vector);

// Test LAPIC interrupt delivery via self-IPI
void test_lapic_self_ipi(platform_t *platform);

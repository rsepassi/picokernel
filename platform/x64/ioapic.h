// x64 I/O APIC (Interrupt Controller)
// Modern interrupt routing for PCI devices

#pragma once

#include <stdint.h>

// IOAPIC Register offsets (indirect addressing via IOREGSEL/IOWIN)
#define IOAPIC_REG_ID 0x00      // IOAPIC ID
#define IOAPIC_REG_VERSION 0x01 // Version and max redirection entries
#define IOAPIC_REG_ARB 0x02     // Arbitration ID
#define IOAPIC_REDTBL_BASE 0x10 // Redirection table base

// Redirection entry flags
#define IOAPIC_DEST_PHYSICAL 0x00000000 // Physical destination mode
#define IOAPIC_DEST_LOGICAL 0x00000800  // Logical destination mode
#define IOAPIC_DELMOD_FIXED 0x00000000  // Fixed delivery mode
#define IOAPIC_DELMOD_LOWEST 0x00000100 // Lowest priority delivery
#define IOAPIC_INTPOL_HIGH 0x00000000   // Active high polarity
#define IOAPIC_INTPOL_LOW 0x00002000    // Active low polarity
#define IOAPIC_TRIGGER_EDGE 0x00000000  // Edge triggered
#define IOAPIC_TRIGGER_LEVEL 0x00008000 // Level triggered
#define IOAPIC_MASK 0x00010000          // Interrupt masked

// Forward declaration and typedef for platform
struct platform_t;
typedef struct platform_t platform_t;

// Forward declaration for IOAPIC (defined in platform_impl.h to avoid circular dependency)
struct ioapic;
typedef struct ioapic ioapic_t;

// Initialize IOAPIC (finds base address from ACPI MADT)
void ioapic_init(platform_t *platform);

// Route an IRQ to a vector
void ioapic_route_irq(platform_t *platform, uint8_t irq, uint8_t vector, uint8_t apic_id);

// Mask/unmask an IRQ
void ioapic_mask_irq(platform_t *platform, uint8_t irq);
void ioapic_unmask_irq(platform_t *platform, uint8_t irq);

// Get IOAPIC info for debugging
const ioapic_t *ioapic_get_info(platform_t *platform);

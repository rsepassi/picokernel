// x64 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include <stddef.h>
#include <stdint.h>

// Include VirtIO headers for complete type definitions
// This is needed to embed VirtIO structures in platform_t
#include "irq_ring.h"
#include "virtio/virtio.h"
#include "virtio/virtio_mmio.h"
#include "virtio/virtio_pci.h"
#include "virtio/virtio_rng.h"

// Forward declarations
struct kernel;
typedef struct kernel kernel_t;

// IOAPIC state structure (defined here to avoid circular dependency with
// ioapic.h)
typedef struct ioapic {
  uint32_t base_addr;  // MMIO base address
  uint32_t gsi_base;   // Global System Interrupt base
  uint8_t id;          // IOAPIC ID
  uint8_t max_entries; // Number of redirection entries
} ioapic_t;

// Timer callback function type
typedef void (*timer_callback_t)(void);

// IRQ routing table entry
#define MAX_IRQ_VECTORS 256

typedef struct {
  void *context;
  void (*handler)(void *context);
} irq_entry_t;

// IDT entries count
#define IDT_ENTRIES 256

// IDT Gate descriptor structure
struct idt_entry {
  uint16_t offset_low;  // Offset bits 0-15
  uint16_t selector;    // Code segment selector
  uint8_t ist;          // Interrupt Stack Table (bits 0-2), reserved (bits 3-7)
  uint8_t flags;        // Type and attributes
  uint16_t offset_mid;  // Offset bits 16-31
  uint32_t offset_high; // Offset bits 32-63
  uint32_t reserved;    // Reserved (must be zero)
} __attribute__((packed));

// IDT Pointer structure
struct idt_ptr {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed));

// ACPI RSDP forward declaration
struct acpi_rsdp;

// x64 platform-specific state
typedef struct platform_t {
  uint32_t last_interrupt;      // Last interrupt reason code
  virtio_rng_dev_t *virtio_rng; // VirtIO-RNG device (NULL if not present)
  kernel_t *kernel;             // Back-pointer to kernel

  // Timer state
  timer_callback_t timer_callback;
  uint64_t timer_start;
  uint64_t lapic_base;
  uint32_t ticks_per_ms;
  uint64_t tsc_start;
  uint64_t tsc_per_ms;

  // VirtIO device structures
  virtio_pci_transport_t virtio_pci_transport;
  virtio_mmio_transport_t virtio_mmio_transport;
  virtio_rng_dev_t virtio_rng_dev;

  // VirtIO queue memory (properly typed)
  virtqueue_memory_t virtqueue_memory;

  // VirtIO debug counters
  volatile uint32_t irq_count;
  volatile uint32_t last_isr_status;

  // IRQ routing table
  irq_entry_t irq_table[MAX_IRQ_VECTORS];

  // x86-specific: IDT and IDTP
  struct idt_entry idt[IDT_ENTRIES];
  struct idt_ptr idtp;

  // x64-specific debug counters
  volatile uint32_t irq_dispatch_count;
  volatile uint32_t irq_eoi_count;

  // ACPI state
  struct acpi_rsdp *rsdp;

  // I/O APIC state
  ioapic_t ioapic;

  // IRQ ring buffer for device interrupts
  kirq_ring_t irq_ring;
} platform_t;

// x64 RNG request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;

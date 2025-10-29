// x32 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include <stddef.h>
#include <stdint.h>

// Include VirtIO headers for complete type definitions
// This is needed to embed VirtIO structures in platform_t
#include "virtio/virtio_mmio.h"
#include "virtio/virtio_pci.h"
#include "virtio/virtio_rng.h"

// Forward declarations
struct kernel;
typedef struct kernel kernel_t;

// Forward declaration for ACPI RSDP
struct acpi_rsdp;

// Timer callback function type
typedef void (*timer_callback_t)(void);

// IRQ handler table entry
typedef struct {
  void *context;
  void (*handler)(void *context);
} irq_entry_t;

// IDT entry structure (x86-specific)
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

// IOAPIC state structure
typedef struct ioapic_t {
  uint32_t base_addr;  // MMIO base address
  uint32_t gsi_base;   // Global System Interrupt base
  uint8_t id;          // IOAPIC ID
  uint8_t max_entries; // Number of redirection entries
} ioapic_t;

// Maximum number of IRQ vectors (x86 supports 256 interrupt vectors)
#define MAX_IRQ_VECTORS 256

// Number of IDT entries
#define IDT_ENTRIES 256

// x32 platform-specific state
typedef struct platform_t {
  // Timer state
  timer_callback_t timer_callback; // Timer callback function pointer
  uint64_t timer_start;            // Timer start time (TSC value)
  uint64_t lapic_base;             // Local APIC base address
  uint32_t ticks_per_ms;           // LAPIC timer ticks per millisecond
  uint64_t tsc_freq;               // TSC frequency in Hz

  // VirtIO device state
  virtio_pci_transport_t virtio_pci_transport;
  virtio_mmio_transport_t virtio_mmio_transport;
  virtio_rng_dev_t virtio_rng;
  virtqueue_memory_t virtqueue_memory; // VirtIO queue memory (properly typed)
  virtio_rng_dev_t
      *virtio_rng_ptr; // Pointer to active RNG device (NULL if not present)

  // VirtIO debug counters (safe to use in IRQ context)
  volatile uint32_t irq_count;
  volatile uint32_t last_isr_status;

  // Interrupt state
  irq_entry_t irq_table[MAX_IRQ_VECTORS];

  // x86-specific: IDT and IDT pointer
  struct idt_entry idt[IDT_ENTRIES];
  struct idt_ptr idtp;

  // ACPI state
  struct acpi_rsdp *rsdp;

  // I/O APIC state
  ioapic_t ioapic;

  // Legacy field (for compatibility)
  uint32_t last_interrupt; // Last interrupt reason code

  // Back-pointer to kernel
  void *kernel;
} platform_t;

// x32 RNG request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;

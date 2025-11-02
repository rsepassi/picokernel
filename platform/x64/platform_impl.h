// x64 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include <stddef.h>
#include <stdint.h>

// Include VirtIO headers for complete type definitions
// This is needed to embed VirtIO structures in platform_t
#include "irq_ring.h"
#include "pvh.h"
#include "virtio/virtio.h"
#include "virtio/virtio_blk.h"
#include "virtio/virtio_mmio.h"
#include "virtio/virtio_net.h"
#include "virtio/virtio_pci.h"
#include "virtio/virtio_rng.h"

// Forward declarations
struct kernel;
typedef struct kernel kernel_t;

// IOAPIC state structure (defined here to avoid circular dependency with
// ioapic.h)
typedef struct ioapic_t {
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
  uint32_t last_interrupt; // Last interrupt reason code
  kernel_t *kernel;        // Back-pointer to kernel

  // Timer state
  timer_callback_t timer_callback;
  uint64_t timer_start;
  uint64_t lapic_base;
  uint32_t ticks_per_ms;
  uint64_t tsc_freq; // TSC frequency in Hz (matches x32)

  // VirtIO device structures
  virtio_pci_transport_t virtio_pci_transport_rng;   // PCI transport for RNG
  virtio_pci_transport_t virtio_pci_transport_blk;   // PCI transport for BLK
  virtio_pci_transport_t virtio_pci_transport_net;   // PCI transport for NET
  virtio_mmio_transport_t virtio_mmio_transport_rng; // MMIO transport for RNG
  virtio_mmio_transport_t virtio_mmio_transport_blk; // MMIO transport for BLK
  virtio_mmio_transport_t virtio_mmio_transport_net; // MMIO transport for NET
  virtio_rng_dev_t virtio_rng;
  virtio_blk_dev_t virtio_blk;
  virtio_net_dev_t virtio_net;

  // VirtIO queue memory (properly typed)
  virtqueue_memory_t virtqueue_rng_memory;    // VirtIO RNG queue memory
  virtqueue_memory_t virtqueue_blk_memory;    // VirtIO BLK queue memory
  virtqueue_memory_t virtqueue_net_rx_memory; // VirtIO NET RX queue memory
  virtqueue_memory_t virtqueue_net_tx_memory; // VirtIO NET TX queue memory

  // Pointers to active devices (NULL if not present)
  virtio_rng_dev_t *virtio_rng_ptr;
  virtio_blk_dev_t *virtio_blk_ptr;
  virtio_net_dev_t *virtio_net_ptr;

  // Block device info (valid if has_block_device = true)
  bool has_block_device;
  uint32_t block_sector_size; // Detected sector size (512, 4096, etc.)
  uint64_t block_capacity;    // Total sectors

  // Network device info (valid if has_net_device = true)
  bool has_net_device;
  uint8_t net_mac_address[6]; // MAC address from device config

  // PCI BAR allocator (for bare-metal BAR assignment)
  uint64_t pci_next_bar_addr; // Next available BAR address

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
  uint32_t last_overflow_count; // Last observed overflow count for logging

  // Memory management (PVH boot)
  struct hvm_start_info *pvh_info;                   // PVH start info structure
  mem_region_t mem_regions[KCONFIG_MAX_MEM_REGIONS]; // Free memory regions
  int num_mem_regions;                               // Number of free regions
} platform_t;

// x64 RNG request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;

// x64 Block request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor chain head index
} kblk_req_platform_t;

// Maximum number of buffers in a network receive ring
#define KNET_MAX_BUFFERS 32

// x64 Network receive request platform-specific fields (VirtIO)
typedef struct {
  uint16_t
      desc_heads[KNET_MAX_BUFFERS]; // Persistent descriptor for each buffer
  bool descriptors_allocated;       // Track if descriptors are set up
} knet_recv_req_platform_t;

// x64 Network send request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor chain head index
} knet_send_req_platform_t;

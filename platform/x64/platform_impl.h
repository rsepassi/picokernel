// x64 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include <stddef.h>
#include <stdint.h>

// Include platform configuration (for VIRTIO_MMIO_BASE, etc.)
#include "platform_config.h"

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
struct platform {
  uint32_t last_interrupt; // Last interrupt reason code
  kernel_t *kernel;        // Back-pointer to kernel

  // Timer state
  timer_callback_t timer_callback;
  uint64_t timer_start;
  uint64_t lapic_base;
  uint8_t lapic_id; // Local APIC ID (extracted from LAPIC ID register)
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

  // MSI-X vector allocator (vectors 128-255 used for MSI-X)
  uint8_t pci_next_msix_vector; // Next available MSI-X CPU vector

  // VirtIO MMIO base address (discovered from ACPI or using default)
  uint64_t virtio_mmio_base; // VirtIO MMIO device base

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
  ioapic_t ioapic[2]; // Support 2 IOAPICs (GSI 0-47)
  uint8_t num_ioapics; // Number of IOAPICs discovered (1 or 2)

  // IRQ ring buffer for device interrupts
  kirq_ring_t irq_ring;
  uint32_t last_overflow_count; // Last observed overflow count for logging

  // Memory management (PVH boot)
  struct hvm_start_info *pvh_info;                   // PVH start info structure
  kregion_t mem_regions[KCONFIG_MAX_MEM_REGIONS]; // Free memory regions
  int num_mem_regions;                               // Number of free regions
  kregion_t *mem_regions_head; // Head of free region list
  kregion_t *mem_regions_tail; // Tail of free region list
};

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

// MMIO inline functions (x86-64 memory barriers)
// Note: 8/16/32-bit functions are provided by platform/shared/mmio.c

static inline void platform_mmio_barrier(void) {
  __asm__ volatile("mfence" ::: "memory");
}

static inline uint64_t platform_mmio_read64(volatile uint64_t *addr) {
  uint64_t val = *addr;
  platform_mmio_barrier();
  return val;
}

static inline void platform_mmio_write64(volatile uint64_t *addr, uint64_t val) {
  *addr = val;
  platform_mmio_barrier();
}

// VirtIO MMIO configuration for QEMU x64
// Defined in platform_config.h: VIRTIO_MMIO_BASE
#define VIRTIO_MMIO_DEVICE_STRIDE 0x200 // 512 bytes per device
#define VIRTIO_MMIO_MAX_DEVICES 32
#define VIRTIO_MMIO_MAGIC 0x74726976 // "virt" in little-endian

// Calculate PCI IRQ number from slot and pin
// x64 QEMU: PCI interrupts use standard INTx swizzling
// For QEMU Q35/microvm, PCI IRQs typically start at 16
static inline uint32_t platform_pci_irq_swizzle(platform_t *platform,
                                                uint8_t slot, uint8_t irq_pin) {
  (void)platform;
  // Standard PCI IRQ swizzle: ((slot + pin - 1) % 4) + base
  uint32_t base_irq = 16; // PCI interrupt base for x64
  return base_irq + ((slot + irq_pin - 1) % 4);
}

// Calculate MMIO IRQ number from device index
// x64 QEMU microvm: IRQs assigned in device creation order (RNG, Block, Net = 5, 6, 7)
// but MMIO addresses are in a different order (Net, Block, RNG)
// This function maps from MMIO slot index to the correct IRQ
static inline uint32_t platform_mmio_irq_number(platform_t *platform,
                                                int index) {
  (void)platform;
  // QEMU creates devices in this order: RNG, Block, Net
  // Devices appear in memory as: Net (slot 0), Block (slot 1), RNG (slot 2)
  // QEMU microvm assigns IRQs by creation order, not slot order:
  //   - RNG (created 1st) -> IRQ 5
  //   - Block (created 2nd) -> IRQ 6
  //   - Net (created 3rd) -> IRQ 7
  // So we need to map: slot 0 (Net) -> 7, slot 1 (Block) -> 6, slot 2 (RNG) -> 5
  const uint32_t slot_to_irq[3] = {7, 6, 5}; // [Net, Block, RNG]
  return slot_to_irq[index];
}

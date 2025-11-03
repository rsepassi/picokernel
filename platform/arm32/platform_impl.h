// ARM32 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include "kconfig_platform.h"
#include <stddef.h>
#include <stdint.h>

// Include VirtIO headers for complete type definitions
// This is needed to embed VirtIO structures in platform_t
#include "irq_ring.h"
#include "virtio/virtio_blk.h"
#include "virtio/virtio_mmio.h"
#include "virtio/virtio_net.h"
#include "virtio/virtio_pci.h"
#include "virtio/virtio_rng.h"

// Forward declarations
struct kernel;
typedef struct kernel kernel_t;

// Timer callback function type
typedef void (*timer_callback_t)(void);

// IRQ handler table entry
typedef struct {
  void *context;
  void (*handler)(void *context);
} irq_entry_t;

// Maximum number of IRQs supported by GICv2
#define MAX_IRQS 1024

// MMIO region descriptor (discovered from FDT)
typedef struct {
  uint32_t base; // Physical base address (32-bit for ARM32)
  uint32_t size; // Size in bytes (aligned to ARM32_PAGE_SIZE)
} mmio_region_t;

// ARM32 platform-specific state
struct platform {
  // Timer state
  uint64_t timer_freq_hz;          // Timer frequency from CNTFRQ
  uint64_t timer_start;            // Start time counter value
  timer_callback_t timer_callback; // Timer callback function pointer

  // VirtIO device state
  virtio_pci_transport_t virtio_pci_transport_rng;   // PCI transport for RNG
  virtio_pci_transport_t virtio_pci_transport_blk;   // PCI transport for BLK
  virtio_pci_transport_t virtio_pci_transport_net;   // PCI transport for NET
  virtio_mmio_transport_t virtio_mmio_transport_rng; // MMIO transport for RNG
  virtio_mmio_transport_t virtio_mmio_transport_blk; // MMIO transport for BLK
  virtio_mmio_transport_t virtio_mmio_transport_net; // MMIO transport for NET
  virtio_rng_dev_t virtio_rng;
  virtio_blk_dev_t virtio_blk;
  virtio_net_dev_t virtio_net;
  virtqueue_memory_t virtqueue_rng_memory;    // VirtIO RNG queue memory
  virtqueue_memory_t virtqueue_blk_memory;    // VirtIO BLK queue memory
  virtqueue_memory_t virtqueue_net_rx_memory; // VirtIO NET RX queue memory
  virtqueue_memory_t virtqueue_net_tx_memory; // VirtIO NET TX queue memory
  virtio_rng_dev_t
      *virtio_rng_ptr; // Pointer to active RNG device (NULL if not present)
  virtio_blk_dev_t
      *virtio_blk_ptr; // Pointer to active BLK device (NULL if not present)
  virtio_net_dev_t
      *virtio_net_ptr; // Pointer to active NET device (NULL if not present)

  // Block device info (valid if has_block_device = true)
  bool has_block_device;
  uint32_t block_sector_size; // Detected sector size (512, 4096, etc.)
  uint64_t block_capacity;    // Total sectors

  // Network device info (valid if has_net_device = true)
  bool has_net_device;
  uint8_t net_mac_address[6]; // MAC address from device config

  // PCI BAR allocator (for bare-metal BAR assignment)
  uint64_t pci_next_bar_addr; // Next available BAR address

  // Interrupt state
  irq_entry_t irq_table[MAX_IRQS];
  kirq_ring_t irq_ring;         // IRQ ring buffer for device interrupts
  uint32_t last_overflow_count; // Last observed overflow count for logging

  // Back-pointer to kernel
  void *kernel;

  // Memory management (discovered at init)
  kregion_t mem_regions[KCONFIG_MAX_MEM_REGIONS]; // Free memory regions
  int num_mem_regions;                            // Number of free regions
  kregion_t *mem_regions_head;                    // Head of free region list
  kregion_t *mem_regions_tail;                    // Tail of free region list
  uintptr_t fdt_base; // Device tree base (to reserve)
  size_t fdt_size;    // Device tree size (from header)

  // MMIO regions (discovered from FDT)
  mmio_region_t mmio_regions[KCONFIG_MAX_MMIO_REGIONS]; // Device MMIO regions
  int num_mmio_regions; // Number of MMIO regions

  // Interrupt controller addresses (discovered from FDT)
  uintptr_t gic_dist_base; // GIC Distributor base address
  uintptr_t gic_cpu_base;  // GIC CPU Interface base address

  // UART address (discovered from FDT)
  uintptr_t uart_base; // UART base address

  // PCI ECAM address (discovered from FDT, if USE_PCI=1)
  uintptr_t pci_ecam_base;   // PCI ECAM base address
  size_t pci_ecam_size;      // PCI ECAM size
  uint64_t pci_mmio_base;    // PCI MMIO range for BAR allocation
  uint64_t pci_mmio_size;    // PCI MMIO range size
  uint64_t virtio_mmio_base; // VirtIO MMIO device base

  // MMU page tables (ARM32 with 4KB pages, short-descriptor format)
  // L1 table: 4096 entries, each covers 1 MB
  uint32_t page_table_l1[4096] __attribute__((aligned(16384)));
  // Pool of L2 tables (each covers 1 MB, 256 entries per table)
  uint32_t page_table_l2_pool[KCONFIG_ARM32_MAX_L2_TABLES][256]
      __attribute__((aligned(1024)));
  int next_l2_table; // L2 allocation counter
};

// ARM32 RNG request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;

// ARM32 Block request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor chain head index
} kblk_req_platform_t;

// Maximum number of buffers in a network receive ring
#define KNET_MAX_BUFFERS 32

// ARM32 Network receive request platform-specific fields (VirtIO)
typedef struct {
  uint16_t
      desc_heads[KNET_MAX_BUFFERS]; // Persistent descriptor for each buffer
  bool descriptors_allocated;       // Track if descriptors are set up
} knet_recv_req_platform_t;

// ARM32 Network send request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor chain head index
} knet_send_req_platform_t;

// ============================================================================
// Platform-specific functions
// ============================================================================

// Initialize UART with address from FDT (called after FDT parsing)
void platform_uart_init(platform_t *platform);

// ============================================================================
// Platform-specific hooks for shared platform code
// ============================================================================

// PCI ECAM base address for QEMU virt machine with highmem=off
// When highmem=off, ECAM is placed within 32-bit addressable space
#define PLATFORM_PCI_ECAM_BASE 0x3f000000UL

// VirtIO MMIO configuration for QEMU ARM virt
#define VIRTIO_MMIO_BASE 0x0a000000ULL
#define VIRTIO_MMIO_DEVICE_STRIDE 0x200 // 512 bytes per device
#define VIRTIO_MMIO_MAX_DEVICES 32
#define VIRTIO_MMIO_MAGIC 0x74726976 // "virt" in little-endian

// Memory barrier for MMIO operations
// Uses DSB SY (Data Synchronization Barrier, full System)
static inline void platform_mmio_barrier(void) {
  __asm__ volatile("dsb sy" ::: "memory");
}

// 64-bit MMIO read for ARM32 (split into two 32-bit reads)
static inline uint64_t platform_mmio_read64(volatile uint64_t *addr) {
  // On ARM32, 64-bit reads must be done as two 32-bit reads
  // Read low word first, then high word
  volatile uint32_t *addr32 = (volatile uint32_t *)addr;
  uint32_t low = addr32[0];
  uint32_t high = addr32[1];
  platform_mmio_barrier();
  return ((uint64_t)high << 32) | low;
}

// 64-bit MMIO write for ARM32 (split into two 32-bit writes)
static inline void platform_mmio_write64(volatile uint64_t *addr,
                                         uint64_t val) {
  // On ARM32, 64-bit writes must be done as two 32-bit writes
  // Write low word first, then high word
  volatile uint32_t *addr32 = (volatile uint32_t *)addr;
  addr32[0] = (uint32_t)val;
  addr32[1] = (uint32_t)(val >> 32);
  platform_mmio_barrier();
}

// Calculate PCI IRQ number from slot and pin
// ARM32 QEMU virt: PCI interrupts use standard INTx swizzling
// Base SPI = 3, rotated by (device + pin - 1) % 4, then offset by 32 for GIC
static inline uint32_t platform_pci_irq_swizzle(platform_t *platform,
                                                uint8_t slot, uint8_t irq_pin) {
  (void)platform;
  uint32_t base_spi = 3; // First PCI interrupt SPI
  uint32_t spi_num = base_spi + ((slot + irq_pin - 1) % 4);
  return 32 + spi_num; // GIC IRQ = 32 + SPI
}

// Calculate MMIO IRQ number from device index
// ARM32 QEMU virt: MMIO IRQs are SPIs starting at offset 16
static inline uint32_t platform_mmio_irq_number(platform_t *platform,
                                                int index) {
  (void)platform;
  uint32_t spi_num = 16 + index; // MMIO IRQ base (SPI 16)
  return 32 + spi_num;           // GIC IRQ = 32 + SPI
}

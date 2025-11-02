// ARM64 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

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

// ARM64 platform-specific state
struct platform_t {
  // Timer state
  uint64_t timer_freq_hz;          // Timer frequency from CNTFRQ_EL0
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
  mem_region_t mem_regions[KCONFIG_MAX_MEM_REGIONS]; // Free memory regions
  int num_mem_regions;                               // Number of free regions
  uintptr_t fdt_base;   // Device tree base (to reserve)
  size_t fdt_size;      // Device tree size (from header)
  uintptr_t kernel_end; // End of kernel (from linker symbol _end)

  // Interrupt controller addresses (discovered from FDT)
  uintptr_t gic_dist_base; // GIC Distributor base address
  uintptr_t gic_cpu_base;  // GIC CPU Interface base address

  // PCI ECAM address (discovered from FDT, if USE_PCI=1)
  uintptr_t pci_ecam_base; // PCI ECAM base address
  size_t pci_ecam_size;    // PCI ECAM size
};
typedef struct platform_t platform_t;

// ARM64 RNG request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;

// ARM64 Block request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor chain head index
} kblk_req_platform_t;

// Maximum number of buffers in a network receive ring
#define KNET_MAX_BUFFERS 32

// ARM64 Network receive request platform-specific fields (VirtIO)
typedef struct {
  uint16_t
      desc_heads[KNET_MAX_BUFFERS]; // Persistent descriptor for each buffer
  bool descriptors_allocated;       // Track if descriptors are set up
} knet_recv_req_platform_t;

// ARM64 Network send request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor chain head index
} knet_send_req_platform_t;

// ============================================================================
// Platform-specific hooks for shared platform code
// ============================================================================

// PCI ECAM base address for QEMU virt machine
#define PLATFORM_PCI_ECAM_BASE 0x4010000000ULL

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

// 64-bit MMIO read for ARM64 (direct read with barrier)
static inline uint64_t platform_mmio_read64(volatile uint64_t *addr) {
  uint64_t val = *addr;
  platform_mmio_barrier();
  return val;
}

// 64-bit MMIO write for ARM64 (direct write with barrier)
static inline void platform_mmio_write64(volatile uint64_t *addr,
                                         uint64_t val) {
  *addr = val;
  platform_mmio_barrier();
}

// Calculate PCI IRQ number from slot and pin
// ARM64 QEMU virt: PCI interrupts use standard INTx swizzling
// Base IRQ = 3 (SPI), rotated by (device + pin - 1) % 4
static inline uint32_t platform_pci_irq_swizzle(platform_t *platform,
                                                uint8_t slot, uint8_t irq_pin) {
  (void)platform;
  uint32_t base_spi = 3; // First PCI interrupt SPI
  uint32_t spi_num = base_spi + ((slot + irq_pin - 1) % 4);
  return 32 + spi_num; // GIC IRQ = 32 + SPI
}

// Calculate MMIO IRQ number from device index
// ARM64 QEMU virt: MMIO IRQs are SPIs starting at offset 16
static inline uint32_t platform_mmio_irq_number(platform_t *platform,
                                                int index) {
  (void)platform;
  uint32_t spi_num = 16 + index; // MMIO IRQ base (SPI 16)
  return 32 + spi_num;           // GIC IRQ = 32 + SPI
}

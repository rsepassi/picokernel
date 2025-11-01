// RISC-V 64-bit Platform Implementation
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

// Maximum number of external interrupts in QEMU virt
#define MAX_IRQS 128

// rv64 platform-specific state
struct platform_t {
  // Timer state
  uint64_t timebase_freq; // Timebase frequency in Hz (from device tree)
  uint64_t timer_start;   // Start time counter value
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
};
typedef struct platform_t platform_t;

// RISC-V RNG request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;

// RISC-V Block request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor chain head index
} kblk_req_platform_t;

// Maximum number of buffers in a network receive ring
#define KNET_MAX_BUFFERS 32

// RISC-V Network receive request platform-specific fields (VirtIO)
typedef struct {
  uint16_t
      desc_heads[KNET_MAX_BUFFERS]; // Persistent descriptor for each buffer
  bool descriptors_allocated;       // Track if descriptors are set up
} knet_recv_req_platform_t;

// RISC-V Network send request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor chain head index
} knet_send_req_platform_t;

// ============================================================================
// Platform-specific hooks for shared platform code
// ============================================================================

// PCI ECAM base address for QEMU RISC-V virt machine
#define PLATFORM_PCI_ECAM_BASE 0x30000000ULL

// VirtIO MMIO configuration for QEMU RISC-V virt
#define VIRTIO_MMIO_BASE 0x10001000ULL
#define VIRTIO_MMIO_DEVICE_STRIDE 0x1000 // 4KB per device
#define VIRTIO_MMIO_MAX_DEVICES 8
#define VIRTIO_MMIO_MAGIC 0x74726976 // "virt" in little-endian

// Memory barrier for MMIO operations
// Uses FENCE instruction for I/O and memory ordering
static inline void platform_mmio_barrier(void) {
  __asm__ volatile("fence iorw, iorw" ::: "memory");
}

// 64-bit MMIO read for RV64 (direct read with barrier)
static inline uint64_t platform_mmio_read64(volatile uint64_t *addr) {
  uint64_t val = *addr;
  platform_mmio_barrier();
  return val;
}

// 64-bit MMIO write for RV64 (direct write with barrier)
static inline void platform_mmio_write64(volatile uint64_t *addr,
                                         uint64_t val) {
  *addr = val;
  platform_mmio_barrier();
}

// Calculate PCI IRQ number from slot and pin
// RISC-V QEMU virt: PCI interrupts use standard INTx swizzling
// Base IRQ = 32, rotated by (device + pin - 1) % 4
static inline uint32_t platform_pci_irq_swizzle(platform_t *platform,
                                                uint8_t slot, uint8_t irq_pin) {
  (void)platform;
  uint32_t base_irq = 32; // First PCI interrupt in PLIC
  return base_irq + ((slot + irq_pin - 1) % 4);
}

// Calculate MMIO IRQ number from device index
// RISC-V QEMU virt: MMIO IRQs start at 1 in PLIC
static inline uint32_t platform_mmio_irq_number(platform_t *platform,
                                                int index) {
  (void)platform;
  return 1 + index; // PLIC external interrupts start at 1
}

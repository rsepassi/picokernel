// rv32 Platform Implementation
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

// Maximum number of IRQs supported by PLIC
#define MAX_IRQS 128

// rv32 platform-specific state
typedef struct platform_t {
  // Timer state
  uint64_t timer_freq;             // Timer frequency in Hz (from devicetree)
  uint64_t ticks_per_ms;           // Precomputed ticks per millisecond
  uint64_t timer_start;            // Start time for timer_get_current_time_ms()
  timer_callback_t timer_callback; // Timer callback function pointer
  volatile int timer_fired; // Flag indicating timer has fired (rv32-specific)

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
  uint32_t pci_next_bar_addr; // Next available BAR address (32-bit for RV32)

  // Interrupt state
  irq_entry_t irq_table[MAX_IRQS];
  kirq_ring_t irq_ring;         // IRQ ring buffer for device interrupts
  uint32_t last_overflow_count; // Last observed overflow count for logging

  // Back-pointer to kernel
  kernel_t *kernel;
} platform_t;

// RV32 RNG request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor index
} krng_req_platform_t;

// RV32 Block request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor chain head index
} kblk_req_platform_t;

// Maximum number of buffers in a network receive ring
#define KNET_MAX_BUFFERS 32

// RV32 Network receive request platform-specific fields (VirtIO)
typedef struct {
  uint16_t
      desc_heads[KNET_MAX_BUFFERS]; // Persistent descriptor for each buffer
  bool descriptors_allocated;       // Track if descriptors are set up
} knet_recv_req_platform_t;

// RV32 Network send request platform-specific fields (VirtIO)
typedef struct {
  uint16_t desc_idx; // VirtIO descriptor chain head index
} knet_send_req_platform_t;

// ============================================================================
// Platform-specific hooks for shared platform code
// ============================================================================

// PCI ECAM base address for QEMU virt machine
#define PLATFORM_PCI_ECAM_BASE 0x30000000UL

// VirtIO MMIO configuration for QEMU RISC-V virt
#define VIRTIO_MMIO_BASE 0x10001000UL
#define VIRTIO_MMIO_DEVICE_STRIDE 0x1000 // 4KB per device
#define VIRTIO_MMIO_MAX_DEVICES 8
#define VIRTIO_MMIO_MAGIC 0x74726976 // "virt" in little-endian

// Memory barrier for MMIO operations
// Uses FENCE instruction to ensure proper ordering on RISC-V's weakly-ordered
// memory model
static inline void platform_mmio_barrier(void) {
  __asm__ volatile("fence iorw, iorw" ::: "memory");
}

// 64-bit MMIO read for RV32 (split into two 32-bit reads)
static inline uint64_t platform_mmio_read64(volatile uint64_t *addr) {
  // On RV32, 64-bit reads must be done as two 32-bit reads
  // Read low word first, then high word
  volatile uint32_t *addr32 = (volatile uint32_t *)addr;
  uint32_t low = addr32[0];
  uint32_t high = addr32[1];
  platform_mmio_barrier();
  return ((uint64_t)high << 32) | low;
}

// 64-bit MMIO write for RV32 (split into two 32-bit writes)
static inline void platform_mmio_write64(volatile uint64_t *addr,
                                         uint64_t val) {
  // On RV32, 64-bit writes must be done as two 32-bit writes
  // Write low word first, then high word
  volatile uint32_t *addr32 = (volatile uint32_t *)addr;
  addr32[0] = (uint32_t)val;
  addr32[1] = (uint32_t)(val >> 32);
  platform_mmio_barrier();
}

// Calculate PCI IRQ number from slot and pin
// RV32 QEMU virt: PCI interrupts use standard INTx swizzling
// Base IRQ = 32, rotated by (device + pin - 1) % 4
static inline uint32_t platform_pci_irq_swizzle(platform_t *platform,
                                                uint8_t slot, uint8_t irq_pin) {
  (void)platform;
  uint32_t base_irq = 32; // First PCI interrupt
  return base_irq + ((slot + irq_pin - 1) % 4);
}

// Calculate MMIO IRQ number from device index
// RV32 QEMU virt: MMIO IRQs start at 1
static inline uint32_t platform_mmio_irq_number(platform_t *platform,
                                                int index) {
  (void)platform;
  return 1 + index; // MMIO IRQ base is 1
}

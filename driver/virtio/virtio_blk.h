// Generic VirtIO Block Device Driver
// Transport-agnostic block driver (works with MMIO or PCI)

#pragma once

#include "kconfig.h"
#include "virtio.h"
#include "virtio_mmio.h"
#include "virtio_pci.h"
#include <stddef.h>
#include <stdint.h>

// Forward declarations
struct kernel;
typedef struct kernel kernel_t;
struct kwork;
typedef struct kwork kwork_t;

// VirtIO Block Device ID
#define VIRTIO_ID_BLOCK 2

// VirtIO Block feature bits
#define VIRTIO_BLK_F_SIZE_MAX 1    // Maximum size of any single segment
#define VIRTIO_BLK_F_SEG_MAX 2     // Maximum number of segments in a request
#define VIRTIO_BLK_F_GEOMETRY 4    // Disk-style geometry available
#define VIRTIO_BLK_F_RO 5          // Device is read-only
#define VIRTIO_BLK_F_BLK_SIZE 6    // Block size of disk available
#define VIRTIO_BLK_F_FLUSH 9       // Cache flush command support
#define VIRTIO_BLK_F_TOPOLOGY 10   // Device exports topology information
#define VIRTIO_BLK_F_CONFIG_WCE 11 // Device can toggle write cache

// VirtIO Block request types
#define VIRTIO_BLK_T_IN 0       // Read
#define VIRTIO_BLK_T_OUT 1      // Write
#define VIRTIO_BLK_T_FLUSH 4    // Flush
#define VIRTIO_BLK_T_DISCARD 11 // Discard (TRIM)

// VirtIO Block request status
#define VIRTIO_BLK_S_OK 0     // Success
#define VIRTIO_BLK_S_IOERR 1  // I/O error
#define VIRTIO_BLK_S_UNSUPP 2 // Unsupported request

// VirtIO Block device configuration (read from device config space)
typedef struct {
  uint64_t capacity; // Device capacity in 512-byte sectors
  uint32_t size_max; // Maximum size of any single segment
  uint32_t seg_max;  // Maximum number of segments in a request
  struct {
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors;
  } geometry;
  uint32_t blk_size; // Block size (typically 512 or 4096)
  struct {
    uint8_t physical_block_exp; // Physical block exponent
    uint8_t alignment_offset;   // Alignment offset
    uint16_t min_io_size;       // Minimum I/O size
    uint32_t opt_io_size;       // Optimal I/O size
  } topology;
  uint8_t writeback; // Write cache mode
  uint8_t unused0[3];
  uint32_t max_discard_sectors;
  uint32_t max_discard_seg;
  uint32_t discard_sector_alignment;
  uint32_t max_write_zeroes_sectors;
  uint32_t max_write_zeroes_seg;
  uint8_t write_zeroes_may_unmap;
  uint8_t unused1[3];
} __attribute__((packed)) virtio_blk_config_t;

// VirtIO Block request header (device-side structure)
typedef struct {
  uint32_t type; // VIRTIO_BLK_T_*
  uint32_t reserved;
  uint64_t sector; // Starting sector
} __attribute__((packed)) virtio_blk_req_header_t;

// Maximum number of outstanding requests
#define VIRTIO_BLK_MAX_REQUESTS 256

// VirtIO Block device state
typedef struct virtio_blk_dev {
  // Device base (MUST be first field for pointer casting!)
  kdevice_base_t base;

  // Transport (either MMIO or PCI)
  void *transport;    // Points to transport-specific structure
  int transport_type; // VIRTIO_TRANSPORT_MMIO or VIRTIO_TRANSPORT_PCI

  // Virtqueue
  virtqueue_t vq;
  virtqueue_memory_t *vq_memory;
  uint16_t queue_size;

  // Device configuration
  uint64_t capacity;    // Total sectors (512-byte units)
  uint32_t sector_size; // Sector size in bytes (512 or 4096)
  uint32_t seg_max;     // Max segments per request

  // Request tracking (constant-time lookup)
  // Use void* to avoid circular dependency
  void *active_requests[VIRTIO_BLK_MAX_REQUESTS];

  // Track number of outstanding requests
  uint16_t outstanding_requests;

  // Kernel reference
  kernel_t *kernel;
} virtio_blk_dev_t;

// Initialize block device with MMIO transport
int virtio_blk_init_mmio(virtio_blk_dev_t *blk, virtio_mmio_transport_t *mmio,
                         virtqueue_memory_t *queue_memory, kernel_t *kernel);

// Initialize block device with PCI transport
int virtio_blk_init_pci(virtio_blk_dev_t *blk, virtio_pci_transport_t *pci,
                        virtqueue_memory_t *queue_memory, kernel_t *kernel);

// Process interrupt (transport-agnostic)
void virtio_blk_process_irq(virtio_blk_dev_t *blk, kernel_t *k);

// Submit work (transport-agnostic)
void virtio_blk_submit_work(virtio_blk_dev_t *blk, kwork_t *submissions,
                            kernel_t *k);

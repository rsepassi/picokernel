// Generic VirtIO Block Device Driver Implementation
// Transport-agnostic block driver

#include "virtio_blk.h"
#include "irq_ring.h"
#include "kapi.h"
#include "kernel.h"
#include "printk.h"
#include <stddef.h>

// Helper structure for VirtIO block request
// This is the on-device request structure that must be physically contiguous
typedef struct {
  virtio_blk_req_header_t header;
  uint8_t status;
} __attribute__((packed)) virtio_blk_req_buf_t;

// Statically allocated request buffers (one per descriptor)
// These must be persistent and properly aligned
static virtio_blk_req_buf_t req_buffers[VIRTIO_BLK_MAX_REQUESTS]
    __attribute__((aligned(4096)));

// Initialize block device with MMIO transport
int virtio_blk_init_mmio(virtio_blk_dev_t *blk, virtio_mmio_transport_t *mmio,
                         virtqueue_memory_t *queue_memory, kernel_t *kernel) {
  blk->transport = mmio;
  blk->transport_type = VIRTIO_TRANSPORT_MMIO;
  blk->kernel = kernel;

  // Reset
  virtio_mmio_reset(mmio);

  // Acknowledge
  virtio_mmio_set_status(mmio, VIRTIO_STATUS_ACKNOWLEDGE);

  // Driver
  virtio_mmio_set_status(mmio,
                         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

  // For legacy (version 1) devices, set GuestPageSize BEFORE queue setup
  if (mmio->version == 1) {
    // Write GuestPageSize register (offset 0x028)
    volatile void *ptr = (volatile void *)(mmio->base + 0x028);
    volatile uint32_t *guest_page_size_reg = (volatile uint32_t *)ptr;
    *guest_page_size_reg = 4096;
  }

  // Read device configuration
  volatile virtio_blk_config_t *config =
      (volatile virtio_blk_config_t *)(mmio->base + 0x100);
  blk->capacity = config->capacity;
  blk->sector_size = config->blk_size;
  if (blk->sector_size == 0) {
    blk->sector_size = 512; // Default to 512 if not specified
  }
  blk->seg_max = config->seg_max;
  if (blk->seg_max == 0) {
    blk->seg_max = 1; // Default to 1 segment
  }

  // Feature negotiation (minimal features for now)
  virtio_mmio_set_features(mmio, 0, 0);

  // Features OK
  uint8_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK;
  virtio_mmio_set_status(mmio, status);

  // Verify features OK
  if (!(virtio_mmio_get_status(mmio) & VIRTIO_STATUS_FEATURES_OK)) {
    printk("      FAILED: features not OK\n");
    return -1;
  }

  // Setup queue
  blk->vq_memory = queue_memory;
  blk->queue_size = virtio_mmio_get_queue_size(mmio, 0);

  if (blk->queue_size > VIRTIO_BLK_MAX_REQUESTS) {
    blk->queue_size = VIRTIO_BLK_MAX_REQUESTS;
  }

  virtqueue_init(&blk->vq, blk->queue_size, queue_memory);
  virtio_mmio_setup_queue(mmio, 0, &blk->vq, blk->queue_size);

  // Driver OK
  status |= VIRTIO_STATUS_DRIVER_OK;
  virtio_mmio_set_status(mmio, status);

  // Check if device failed
  if (virtio_mmio_get_status(mmio) & 0x80) { // VIRTIO_STATUS_FAILED
    printk("      FAILED: device failed\n");
    return -1;
  }

  // Clear request tracking and outstanding counter
  blk->outstanding_requests = 0;
  for (int i = 0; i < VIRTIO_BLK_MAX_REQUESTS; i++) {
    blk->active_requests[i] = NULL;
  }

  return 0;
}

// Initialize block device with PCI transport
int virtio_blk_init_pci(virtio_blk_dev_t *blk, virtio_pci_transport_t *pci,
                        virtqueue_memory_t *queue_memory, kernel_t *kernel) {
  blk->transport = pci;
  blk->transport_type = VIRTIO_TRANSPORT_PCI;
  blk->kernel = kernel;

  // Reset
  virtio_pci_reset(pci);

  // Acknowledge
  virtio_pci_set_status(pci, VIRTIO_STATUS_ACKNOWLEDGE);

  // Driver
  virtio_pci_set_status(pci, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

  // Read device configuration
  volatile virtio_blk_config_t *config =
      (volatile virtio_blk_config_t *)pci->device_cfg;
  blk->capacity = config->capacity;
  blk->sector_size = config->blk_size;
  if (blk->sector_size == 0) {
    blk->sector_size = 512; // Default to 512 if not specified
  }
  blk->seg_max = config->seg_max;
  if (blk->seg_max == 0) {
    blk->seg_max = 1; // Default to 1 segment
  }

  // Feature negotiation (minimal features for now)
  virtio_pci_set_features(pci, 0, 0);

  // Features OK
  uint8_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK;
  virtio_pci_set_status(pci, status);

  // Verify features OK
  if (!(virtio_pci_get_status(pci) & VIRTIO_STATUS_FEATURES_OK)) {
    return -1;
  }

  // Configure device to use legacy interrupts (not MSI-X)
  volatile virtio_pci_common_cfg_t *common_cfg = pci->common_cfg;
  common_cfg->msix_config = 0xFFFF;

  // Setup queue
  blk->vq_memory = queue_memory;
  blk->queue_size = virtio_pci_get_queue_size(pci, 0);
  if (blk->queue_size > VIRTIO_BLK_MAX_REQUESTS) {
    blk->queue_size = VIRTIO_BLK_MAX_REQUESTS;
  }

  virtqueue_init(&blk->vq, blk->queue_size, queue_memory);
  virtio_pci_setup_queue(pci, 0, &blk->vq, blk->queue_size);

  // Driver OK
  status |= VIRTIO_STATUS_DRIVER_OK;
  virtio_pci_set_status(pci, status);

  // Clear request tracking and outstanding counter
  blk->outstanding_requests = 0;
  for (int i = 0; i < VIRTIO_BLK_MAX_REQUESTS; i++) {
    blk->active_requests[i] = NULL;
  }

  return 0;
}

// Submit work (transport-agnostic)
void virtio_blk_submit_work(virtio_blk_dev_t *blk, kwork_t *submissions,
                            kernel_t *k) {
  int submitted = 0;

  kwork_t *work = submissions;
  while (work != NULL) {
    kwork_t *next = work->next;

    // Check if this is a block operation
    if (work->op == KWORK_OP_BLOCK_READ || work->op == KWORK_OP_BLOCK_WRITE ||
        work->op == KWORK_OP_BLOCK_FLUSH) {
      kblk_req_t *req = CONTAINER_OF(work, kblk_req_t, work);

      // Validate request
      if (work->op != KWORK_OP_BLOCK_FLUSH) {
        // Read/Write operations need segments
        if (req->segments == NULL || req->num_segments == 0) {
          kplatform_complete_work(k, work, KERR_INVALID);
          work = next;
          continue;
        }

        // Only support single segment for now
        if (req->num_segments > 1) {
          kplatform_complete_work(k, work, KERR_INVALID);
          work = next;
          continue;
        }

        // Validate buffer alignment (must be 4K-aligned)
        if (((uint64_t)req->segments[0].buffer & 0xFFF) != 0) {
          kplatform_complete_work(k, work, KERR_INVALID);
          work = next;
          continue;
        }
      }

      // Allocate descriptor
      uint16_t desc_idx = virtqueue_alloc_desc(&blk->vq);

      if (desc_idx == VIRTQUEUE_NO_DESC) {
        // Queue full - immediate failure with backpressure
        kplatform_complete_work(k, work, KERR_NO_SPACE);
        work = next;
        continue;
      }

      // Setup request buffer
      virtio_blk_req_buf_t *req_buf = &req_buffers[desc_idx];

      if (work->op == KWORK_OP_BLOCK_FLUSH) {
        req_buf->header.type = VIRTIO_BLK_T_FLUSH;
        req_buf->header.reserved = 0;
        req_buf->header.sector = 0;
      } else {
        req_buf->header.type = (work->op == KWORK_OP_BLOCK_READ)
                                   ? VIRTIO_BLK_T_IN
                                   : VIRTIO_BLK_T_OUT;
        req_buf->header.reserved = 0;
        req_buf->header.sector = req->segments[0].sector;
      }

      req_buf->status = 0xFF; // Initialize to invalid status

      // Build descriptor chain
      // For read/write: header -> data buffer -> status
      // For flush: header -> status

      uint16_t header_desc = desc_idx;
      uint16_t data_desc = VIRTQUEUE_NO_DESC;
      uint16_t status_desc = VIRTQUEUE_NO_DESC;

      if (work->op != KWORK_OP_BLOCK_FLUSH) {
        // Allocate data and status descriptors
        data_desc = virtqueue_alloc_desc(&blk->vq);
        if (data_desc == VIRTQUEUE_NO_DESC) {
          virtqueue_free_desc(&blk->vq, header_desc);
          kplatform_complete_work(k, work, KERR_NO_SPACE);
          work = next;
          continue;
        }

        status_desc = virtqueue_alloc_desc(&blk->vq);
        if (status_desc == VIRTQUEUE_NO_DESC) {
          virtqueue_free_desc(&blk->vq, header_desc);
          virtqueue_free_desc(&blk->vq, data_desc);
          kplatform_complete_work(k, work, KERR_NO_SPACE);
          work = next;
          continue;
        }

        // Setup header descriptor (device reads)
        virtqueue_add_desc(&blk->vq, header_desc, (uint64_t)&req_buf->header,
                           sizeof(virtio_blk_req_header_t), VIRTQ_DESC_F_NEXT);
        blk->vq.desc[header_desc].next = data_desc;

        // Setup data descriptor
        uint32_t data_len = req->segments[0].num_sectors * blk->sector_size;
        uint16_t data_flags = VIRTQ_DESC_F_NEXT;
        if (work->op == KWORK_OP_BLOCK_READ) {
          data_flags |= VIRTQ_DESC_F_WRITE; // Device writes to buffer
        }
        virtqueue_add_desc(&blk->vq, data_desc,
                           (uint64_t)req->segments[0].buffer, data_len,
                           data_flags);
        blk->vq.desc[data_desc].next = status_desc;

        // Setup status descriptor (device writes)
        virtqueue_add_desc(&blk->vq, status_desc, (uint64_t)&req_buf->status, 1,
                           VIRTQ_DESC_F_WRITE);
      } else {
        // Flush: just header -> status
        status_desc = virtqueue_alloc_desc(&blk->vq);
        if (status_desc == VIRTQUEUE_NO_DESC) {
          virtqueue_free_desc(&blk->vq, header_desc);
          kplatform_complete_work(k, work, KERR_NO_SPACE);
          work = next;
          continue;
        }

        // Setup header descriptor (device reads)
        virtqueue_add_desc(&blk->vq, header_desc, (uint64_t)&req_buf->header,
                           sizeof(virtio_blk_req_header_t), VIRTQ_DESC_F_NEXT);
        blk->vq.desc[header_desc].next = status_desc;

        // Setup status descriptor (device writes)
        virtqueue_add_desc(&blk->vq, status_desc, (uint64_t)&req_buf->status, 1,
                           VIRTQ_DESC_F_WRITE);
      }

      // Add to available ring (use header descriptor as chain head)
      virtqueue_add_avail(&blk->vq, header_desc);

      // Track request for completion (constant-time lookup)
      req->platform.desc_idx = header_desc;
      blk->active_requests[header_desc] = req;

      // Mark as live
      work->state = KWORK_STATE_LIVE;
      blk->outstanding_requests++;
      submitted++;
    }

    work = next;
  }

  // Kick device once for all descriptors (bulk submission)
  if (submitted > 0) {
    // Notify device (transport-specific implementation)
    if (blk->transport_type == VIRTIO_TRANSPORT_MMIO) {
      virtio_mmio_notify_queue((virtio_mmio_transport_t *)blk->transport, 0);
    } else if (blk->transport_type == VIRTIO_TRANSPORT_PCI) {
      virtio_pci_notify_queue((virtio_pci_transport_t *)blk->transport, 0);
    }
    // Block device uses interrupts only (no polling)
  }
}

// Process interrupt (transport-agnostic)
void virtio_blk_process_irq(virtio_blk_dev_t *blk, kernel_t *k) {
  // Process all available completions
  while (virtqueue_has_used(&blk->vq)) {
    uint16_t desc_idx;
    uint32_t len;
    virtqueue_get_used(&blk->vq, &desc_idx, &len);

    kblk_req_t *req = blk->active_requests[desc_idx];
    if (req != NULL) {
      // Check status byte
      virtio_blk_req_buf_t *req_buf = &req_buffers[desc_idx];
      kerr_t result = KERR_OK;

      if (req_buf->status != VIRTIO_BLK_S_OK) {
        if (req_buf->status == VIRTIO_BLK_S_IOERR) {
          result = KERR_IO_ERROR;
        } else if (req_buf->status == VIRTIO_BLK_S_UNSUPP) {
          result = KERR_INVALID;
        } else {
          result = KERR_IO_ERROR; // Unknown error
        }
      }

      // Update completed sectors for read/write operations
      if (result == KERR_OK && req->work.op != KWORK_OP_BLOCK_FLUSH) {
        req->segments[0].completed_sectors = req->segments[0].num_sectors;
      }

      kplatform_complete_work(k, &req->work, result);
      blk->active_requests[desc_idx] = NULL;
      blk->outstanding_requests--;

      // Free descriptor chain (need to walk the chain)
      uint16_t curr_desc = desc_idx;
      while (curr_desc != VIRTQUEUE_NO_DESC) {
        uint16_t next_desc = blk->vq.desc[curr_desc].next;
        int has_next = blk->vq.desc[curr_desc].flags & VIRTQ_DESC_F_NEXT;
        virtqueue_free_desc(&blk->vq, curr_desc);
        if (!has_next) {
          break;
        }
        curr_desc = next_desc;
      }
    } else {
      // Free orphaned descriptor
      virtqueue_free_desc(&blk->vq, desc_idx);
    }
  }

  // Block device uses interrupts only (no re-enqueue for polling)
}

// Generic VirtIO Network Device Driver Implementation
// Transport-agnostic network driver

#include "virtio_net.h"
#include "irq_ring.h"
#include "kapi.h"
#include "kernel.h"
#include "printk.h"
#include <stddef.h>

// Statically allocated network header buffers (one per descriptor)
// Each packet needs a virtio_net_hdr_t prepended
static virtio_net_hdr_t rx_hdr_buffers[VIRTIO_NET_MAX_REQUESTS]
    __attribute__((aligned(64)));
static virtio_net_hdr_t tx_hdr_buffers[VIRTIO_NET_MAX_REQUESTS]
    __attribute__((aligned(64)));

// Initialize network device with MMIO transport
int virtio_net_init_mmio(virtio_net_dev_t *net, virtio_mmio_transport_t *mmio,
                         virtqueue_memory_t *rx_queue_memory,
                         virtqueue_memory_t *tx_queue_memory,
                         kernel_t *kernel) {
  net->transport = mmio;
  net->transport_type = VIRTIO_TRANSPORT_MMIO;
  net->kernel = kernel;

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

  // Feature negotiation (request MAC feature)
  uint32_t features = (1 << VIRTIO_NET_F_MAC);
  virtio_mmio_set_features(mmio, features, 0);

  // Features OK
  uint8_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK;
  virtio_mmio_set_status(mmio, status);

  // Verify features OK
  if (!(virtio_mmio_get_status(mmio) & VIRTIO_STATUS_FEATURES_OK)) {
    printk("      FAILED: features not OK\n");
    return -1;
  }

  // Read device configuration (MAC address) after feature negotiation
  volatile virtio_net_config_t *config =
      (volatile virtio_net_config_t *)(mmio->base + 0x100);
  for (int i = 0; i < 6; i++) {
    net->mac_address[i] = config->mac[i];
  }

  // Setup RX queue (queue 0)
  net->rx_vq_memory = rx_queue_memory;
  net->queue_size = virtio_mmio_get_queue_size(mmio, VIRTIO_NET_VQ_RX);
  if (net->queue_size > VIRTIO_NET_MAX_REQUESTS) {
    net->queue_size = VIRTIO_NET_MAX_REQUESTS;
  }
  virtqueue_init(&net->rx_vq, net->queue_size, rx_queue_memory);
  virtio_mmio_setup_queue(mmio, VIRTIO_NET_VQ_RX, &net->rx_vq, net->queue_size);

  // Setup TX queue (queue 1)
  net->tx_vq_memory = tx_queue_memory;
  uint16_t tx_queue_size = virtio_mmio_get_queue_size(mmio, VIRTIO_NET_VQ_TX);
  if (tx_queue_size > VIRTIO_NET_MAX_REQUESTS) {
    tx_queue_size = VIRTIO_NET_MAX_REQUESTS;
  }
  virtqueue_init(&net->tx_vq, tx_queue_size, tx_queue_memory);
  virtio_mmio_setup_queue(mmio, VIRTIO_NET_VQ_TX, &net->tx_vq, tx_queue_size);

  // Driver OK
  status |= VIRTIO_STATUS_DRIVER_OK;
  virtio_mmio_set_status(mmio, status);

  // Check if device failed
  if (virtio_mmio_get_status(mmio) & 0x80) { // VIRTIO_STATUS_FAILED
    printk("      FAILED: device failed\n");
    return -1;
  }

  // Clear request tracking
  net->outstanding_rx_requests = 0;
  net->outstanding_tx_requests = 0;
  net->standing_recv_req = NULL;
  for (int i = 0; i < VIRTIO_NET_MAX_REQUESTS; i++) {
    net->active_rx_requests[i].req = NULL;
    net->active_rx_requests[i].buffer_index = 0;
    net->active_tx_requests[i] = NULL;
  }

  return 0;
}

// Initialize network device with PCI transport
int virtio_net_init_pci(virtio_net_dev_t *net, virtio_pci_transport_t *pci,
                        virtqueue_memory_t *rx_queue_memory,
                        virtqueue_memory_t *tx_queue_memory, kernel_t *kernel) {
  net->transport = pci;
  net->transport_type = VIRTIO_TRANSPORT_PCI;
  net->kernel = kernel;

  // Reset
  virtio_pci_reset(pci);

  // Acknowledge
  virtio_pci_set_status(pci, VIRTIO_STATUS_ACKNOWLEDGE);

  // Driver
  virtio_pci_set_status(pci, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

  // Feature negotiation (request MAC feature)
  uint32_t features = (1 << VIRTIO_NET_F_MAC);
  virtio_pci_set_features(pci, features, 0);

  // Features OK
  uint8_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK;
  virtio_pci_set_status(pci, status);

  // Verify features OK
  if (!(virtio_pci_get_status(pci) & VIRTIO_STATUS_FEATURES_OK)) {
    return -1;
  }

  // Read device configuration (MAC address) after feature negotiation
  volatile virtio_net_config_t *config =
      (volatile virtio_net_config_t *)pci->device_cfg;
  for (int i = 0; i < 6; i++) {
    net->mac_address[i] = config->mac[i];
  }

  // Configure device to use legacy interrupts (not MSI-X)
  volatile virtio_pci_common_cfg_t *common_cfg = pci->common_cfg;
  common_cfg->msix_config = 0xFFFF;

  // Setup RX queue (queue 0)
  net->rx_vq_memory = rx_queue_memory;
  net->queue_size = virtio_pci_get_queue_size(pci, VIRTIO_NET_VQ_RX);
  if (net->queue_size > VIRTIO_NET_MAX_REQUESTS) {
    net->queue_size = VIRTIO_NET_MAX_REQUESTS;
  }
  virtqueue_init(&net->rx_vq, net->queue_size, rx_queue_memory);
  virtio_pci_setup_queue(pci, VIRTIO_NET_VQ_RX, &net->rx_vq, net->queue_size);

  // Setup TX queue (queue 1)
  net->tx_vq_memory = tx_queue_memory;
  uint16_t tx_queue_size = virtio_pci_get_queue_size(pci, VIRTIO_NET_VQ_TX);
  if (tx_queue_size > VIRTIO_NET_MAX_REQUESTS) {
    tx_queue_size = VIRTIO_NET_MAX_REQUESTS;
  }
  virtqueue_init(&net->tx_vq, tx_queue_size, tx_queue_memory);
  virtio_pci_setup_queue(pci, VIRTIO_NET_VQ_TX, &net->tx_vq, tx_queue_size);

  // Driver OK
  status |= VIRTIO_STATUS_DRIVER_OK;
  virtio_pci_set_status(pci, status);

  // Clear request tracking
  net->outstanding_rx_requests = 0;
  net->outstanding_tx_requests = 0;
  net->standing_recv_req = NULL;
  for (int i = 0; i < VIRTIO_NET_MAX_REQUESTS; i++) {
    net->active_rx_requests[i].req = NULL;
    net->active_rx_requests[i].buffer_index = 0;
    net->active_tx_requests[i] = NULL;
  }

  return 0;
}

// Helper function to submit a single RX buffer to the device
static kerr_t submit_rx_buffer(virtio_net_dev_t *net, knet_recv_req_t *req,
                               size_t buffer_index) {
  knet_buffer_t *buf = &req->buffers[buffer_index];
  uint16_t hdr_desc, data_desc;

  // Check if descriptors are already allocated for this buffer (standing work)
  if (req->platform.desc_heads[buffer_index] != VIRTQUEUE_NO_DESC) {
    // Reuse existing descriptors (persistent for standing work)
    hdr_desc = req->platform.desc_heads[buffer_index];
    data_desc = net->rx_vq.desc[hdr_desc].next;

    // Descriptors are already set up, just add to available ring
  } else {
    // Allocate new descriptor chain: header -> data
    hdr_desc = virtqueue_alloc_desc(&net->rx_vq);
    if (hdr_desc == VIRTQUEUE_NO_DESC) {
      return KERR_NO_SPACE;
    }

    data_desc = virtqueue_alloc_desc(&net->rx_vq);
    if (data_desc == VIRTQUEUE_NO_DESC) {
      virtqueue_free_desc(&net->rx_vq, hdr_desc);
      return KERR_NO_SPACE;
    }

    // Setup header descriptor (device writes)
    virtio_net_hdr_t *hdr = &rx_hdr_buffers[hdr_desc];
    virtqueue_add_desc(&net->rx_vq, hdr_desc, (uint64_t)hdr,
                       sizeof(virtio_net_hdr_t),
                       VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT);
    net->rx_vq.desc[hdr_desc].next = data_desc;

    // Setup data descriptor (device writes)
    virtqueue_add_desc(&net->rx_vq, data_desc, (uint64_t)buf->buffer,
                       buf->buffer_size, VIRTQ_DESC_F_WRITE);

    // Store descriptor head for future reuse
    req->platform.desc_heads[buffer_index] = hdr_desc;
  }

  // Add to available ring
  virtqueue_add_avail(&net->rx_vq, hdr_desc);

  // Track request
  net->active_rx_requests[hdr_desc].req = req;
  net->active_rx_requests[hdr_desc].buffer_index = buffer_index;
  net->outstanding_rx_requests++;

  return KERR_OK;
}

// Submit work (transport-agnostic)
void virtio_net_submit_work(virtio_net_dev_t *net, kwork_t *submissions,
                            kernel_t *k) {
  int submitted_rx = 0;
  int submitted_tx = 0;

  kwork_t *work = submissions;
  while (work != NULL) {
    kwork_t *next = work->next;

    if (work->op == KWORK_OP_NET_RECV) {
      knet_recv_req_t *req = CONTAINER_OF(work, knet_recv_req_t, work);

      // Validate request
      if (req->buffers == NULL || req->num_buffers == 0) {
        kplatform_complete_work(k, work, KERR_INVALID);
        work = next;
        continue;
      }

      // Check if this is standing work
      if (work->flags & KWORK_FLAG_STANDING) {
        // Check buffer count limit
        if (req->num_buffers > KNET_MAX_BUFFERS) {
          kplatform_complete_work(k, work, KERR_INVALID);
          work = next;
          continue;
        }

        // Store the standing request for later buffer releases
        net->standing_recv_req = req;

        // Initialize descriptor tracking if not already done
        if (!req->platform.descriptors_allocated) {
          for (size_t i = 0; i < KNET_MAX_BUFFERS; i++) {
            req->platform.desc_heads[i] = VIRTQUEUE_NO_DESC;
          }
          req->platform.descriptors_allocated = true;
        }

        // Submit all buffers in the ring
        bool all_submitted = true;
        for (size_t i = 0; i < req->num_buffers; i++) {
          kerr_t result = submit_rx_buffer(net, req, i);
          if (result != KERR_OK) {
            all_submitted = false;
            break;
          }
          submitted_rx++;
        }

        if (!all_submitted) {
          // Failed to submit all buffers
          kplatform_complete_work(k, work, KERR_NO_SPACE);
          work = next;
          continue;
        }

        // Mark as live (standing work stays live)
        work->state = KWORK_STATE_LIVE;
      } else {
        // Non-standing receive not supported
        kplatform_complete_work(k, work, KERR_INVALID);
        work = next;
        continue;
      }

    } else if (work->op == KWORK_OP_NET_SEND) {
      knet_send_req_t *req = CONTAINER_OF(work, knet_send_req_t, work);

      // Validate request
      if (req->packets == NULL || req->num_packets == 0) {
        kplatform_complete_work(k, work, KERR_INVALID);
        work = next;
        continue;
      }

      // For now, only support sending one packet at a time
      if (req->num_packets > 1) {
        kplatform_complete_work(k, work, KERR_INVALID);
        work = next;
        continue;
      }

      knet_buffer_t *pkt = &req->packets[0];

      // Allocate descriptor chain: header -> data
      uint16_t hdr_desc = virtqueue_alloc_desc(&net->tx_vq);
      if (hdr_desc == VIRTQUEUE_NO_DESC) {
        kplatform_complete_work(k, work, KERR_NO_SPACE);
        work = next;
        continue;
      }

      uint16_t data_desc = virtqueue_alloc_desc(&net->tx_vq);
      if (data_desc == VIRTQUEUE_NO_DESC) {
        virtqueue_free_desc(&net->tx_vq, hdr_desc);
        kplatform_complete_work(k, work, KERR_NO_SPACE);
        work = next;
        continue;
      }

      // Setup header descriptor (device reads, no flags needed)
      virtio_net_hdr_t *hdr = &tx_hdr_buffers[hdr_desc];
      hdr->flags = 0;
      hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;
      hdr->hdr_len = 0;
      hdr->gso_size = 0;
      hdr->csum_start = 0;
      hdr->csum_offset = 0;
      hdr->num_buffers = 0;

      virtqueue_add_desc(&net->tx_vq, hdr_desc, (uint64_t)hdr,
                         sizeof(virtio_net_hdr_t), VIRTQ_DESC_F_NEXT);
      net->tx_vq.desc[hdr_desc].next = data_desc;

      // Setup data descriptor (device reads)
      virtqueue_add_desc(&net->tx_vq, data_desc, (uint64_t)pkt->buffer,
                         pkt->buffer_size, 0);

      // Add to available ring
      virtqueue_add_avail(&net->tx_vq, hdr_desc);

      // Track request
      req->platform.desc_idx = hdr_desc;
      net->active_tx_requests[hdr_desc] = req;

      // Mark as live
      work->state = KWORK_STATE_LIVE;
      net->outstanding_tx_requests++;
      submitted_tx++;
    }

    work = next;
  }

  // Kick RX device if we submitted any RX buffers
  if (submitted_rx > 0) {
    __sync_synchronize();
    if (net->transport_type == VIRTIO_TRANSPORT_MMIO) {
      virtio_mmio_notify_queue((virtio_mmio_transport_t *)net->transport,
                               VIRTIO_NET_VQ_RX);
    } else if (net->transport_type == VIRTIO_TRANSPORT_PCI) {
      virtio_pci_notify_queue((virtio_pci_transport_t *)net->transport,
                              &net->rx_vq);
    }
  }

  // Kick TX device if we submitted any TX packets
  if (submitted_tx > 0) {
    __sync_synchronize();
    if (net->transport_type == VIRTIO_TRANSPORT_MMIO) {
      virtio_mmio_notify_queue((virtio_mmio_transport_t *)net->transport,
                               VIRTIO_NET_VQ_TX);
    } else if (net->transport_type == VIRTIO_TRANSPORT_PCI) {
      virtio_pci_notify_queue((virtio_pci_transport_t *)net->transport,
                              &net->tx_vq);
    }
  }
}

// Process interrupt (transport-agnostic)
void virtio_net_process_irq(virtio_net_dev_t *net, kernel_t *k) {
  // Process RX completions
  while (virtqueue_has_used(&net->rx_vq)) {
    uint16_t desc_idx;
    uint32_t len;
    virtqueue_get_used(&net->rx_vq, &desc_idx, &len);

    knet_recv_req_t *req = net->active_rx_requests[desc_idx].req;
    if (req != NULL) {
      // Get which buffer this descriptor corresponds to
      size_t buffer_index = net->active_rx_requests[desc_idx].buffer_index;

      // Update packet length (subtract header size)
      if (len > sizeof(virtio_net_hdr_t)) {
        req->buffers[buffer_index].packet_length =
            len - sizeof(virtio_net_hdr_t);
      } else {
        req->buffers[buffer_index].packet_length = 0;
      }

      // Complete work (for standing work, callback fires but work stays LIVE)
      req->buffer_index = buffer_index; // Set for callback
      kplatform_complete_work(k, &req->work, KERR_OK);

      // Clear tracking
      net->active_rx_requests[desc_idx].req = NULL;
      net->active_rx_requests[desc_idx].buffer_index = 0;
      net->outstanding_rx_requests--;

      // For standing work, don't free descriptors (they're persistent)
      // For one-shot work, free the descriptor chain
      if (req != net->standing_recv_req) {
        // One-shot work - free descriptor chain
        uint16_t curr_desc = desc_idx;
        while (curr_desc != VIRTQUEUE_NO_DESC) {
          uint16_t next_desc = net->rx_vq.desc[curr_desc].next;
          int has_next = net->rx_vq.desc[curr_desc].flags & VIRTQ_DESC_F_NEXT;
          virtqueue_free_desc(&net->rx_vq, curr_desc);
          if (!has_next) {
            break;
          }
          curr_desc = next_desc;
        }
      }
      // Note: Standing work descriptors stay allocated, buffer is "with user"
    } else {
      // Free orphaned descriptor (shouldn't happen, but defensive)
      uint16_t curr_desc = desc_idx;
      while (curr_desc != VIRTQUEUE_NO_DESC) {
        uint16_t next_desc = net->rx_vq.desc[curr_desc].next;
        int has_next = net->rx_vq.desc[curr_desc].flags & VIRTQ_DESC_F_NEXT;
        virtqueue_free_desc(&net->rx_vq, curr_desc);
        if (!has_next) {
          break;
        }
        curr_desc = next_desc;
      }
    }
  }

  // Process TX completions
  while (virtqueue_has_used(&net->tx_vq)) {
    uint16_t desc_idx;
    uint32_t len;
    virtqueue_get_used(&net->tx_vq, &desc_idx, &len);

    knet_send_req_t *req = net->active_tx_requests[desc_idx];
    if (req != NULL) {
      // Update packets sent
      req->packets_sent = 1; // We only send one packet at a time for now

      kplatform_complete_work(k, &req->work, KERR_OK);
      net->active_tx_requests[desc_idx] = NULL;
      net->outstanding_tx_requests--;

      // Free descriptor chain
      uint16_t curr_desc = desc_idx;
      while (curr_desc != VIRTQUEUE_NO_DESC) {
        uint16_t next_desc = net->tx_vq.desc[curr_desc].next;
        int has_next = net->tx_vq.desc[curr_desc].flags & VIRTQ_DESC_F_NEXT;
        virtqueue_free_desc(&net->tx_vq, curr_desc);
        if (!has_next) {
          break;
        }
        curr_desc = next_desc;
      }
    } else {
      // Free orphaned descriptor
      virtqueue_free_desc(&net->tx_vq, desc_idx);
    }
  }
}

// Release a receive buffer back to the ring after user processes it
// This is called by the platform layer after user calls knet_buffer_release()
void virtio_net_buffer_release(virtio_net_dev_t *net, void *req_ptr,
                               size_t buffer_index) {
  // Cast to proper type
  knet_recv_req_t *req = (knet_recv_req_t *)req_ptr;

  // Safety checks
  if (net == NULL || req == NULL) {
    return;
  }

  // Verify this is the active standing recv request
  if (net->standing_recv_req != req) {
    return; // Not the active standing request
  }

  // Verify buffer index is valid
  if (buffer_index >= req->num_buffers) {
    return; // Invalid buffer index
  }

  // Get the pre-allocated persistent descriptor
  uint16_t desc_head = req->platform.desc_heads[buffer_index];
  if (desc_head == VIRTQUEUE_NO_DESC) {
    return; // Descriptor not allocated (shouldn't happen)
  }

  // Add descriptor back to available ring
  virtqueue_add_avail(&net->rx_vq, desc_head);

  // Update tracking (buffer is now back in device queue)
  net->active_rx_requests[desc_head].req = req;
  net->active_rx_requests[desc_head].buffer_index = buffer_index;
  net->outstanding_rx_requests++;

  // Notify device that new buffer is available
  __sync_synchronize();
  if (net->transport_type == VIRTIO_TRANSPORT_MMIO) {
    virtio_mmio_notify_queue((virtio_mmio_transport_t *)net->transport,
                             VIRTIO_NET_VQ_RX);
  } else if (net->transport_type == VIRTIO_TRANSPORT_PCI) {
    virtio_pci_notify_queue((virtio_pci_transport_t *)net->transport,
                            &net->rx_vq);
  }
}

// Cancel network work (for standing recv work cleanup)
void virtio_net_cancel_work(virtio_net_dev_t *net, kwork_t *work, kernel_t *k) {
  // Only NET_RECV supports cancellation (standing work)
  if (work->op == KWORK_OP_NET_RECV) {
    knet_recv_req_t *req = CONTAINER_OF(work, knet_recv_req_t, work);

    // Clear standing recv pointer if this is the active one
    if (net->standing_recv_req == req) {
      net->standing_recv_req = NULL;
    }

    // Free any outstanding descriptors for this request
    // Walk through all descriptor slots to find ones belonging to this request
    for (int i = 0; i < VIRTIO_NET_MAX_REQUESTS; i++) {
      if (net->active_rx_requests[i].req == req) {
        // Clear tracking
        net->active_rx_requests[i].req = NULL;
        net->active_rx_requests[i].buffer_index = 0;
        net->outstanding_rx_requests--;

        // Note: Don't free descriptors here - they're already removed from
        // the available and used rings by the device or were never added
      }
    }

    // Free all persistent descriptors for this request
    if (req->platform.descriptors_allocated) {
      for (size_t i = 0; i < req->num_buffers; i++) {
        uint16_t desc_head = req->platform.desc_heads[i];
        if (desc_head != VIRTQUEUE_NO_DESC) {
          // Free descriptor chain (header + data)
          uint16_t curr_desc = desc_head;
          while (curr_desc != VIRTQUEUE_NO_DESC) {
            uint16_t next_desc = net->rx_vq.desc[curr_desc].next;
            int has_next = net->rx_vq.desc[curr_desc].flags & VIRTQ_DESC_F_NEXT;
            virtqueue_free_desc(&net->rx_vq, curr_desc);
            if (!has_next) {
              break;
            }
            curr_desc = next_desc;
          }
          req->platform.desc_heads[i] = VIRTQUEUE_NO_DESC;
        }
      }
      req->platform.descriptors_allocated = false;
    }

    // Cancellation successful - notify user (callback fires with
    // KERR_CANCELLED)
    kplatform_cancel_work(k, work);
  }
  // Note: KWORK_OP_NET_SEND doesn't support cancellation (one-shot work)
  // Silently ignore cancellation request - do not call kplatform_cancel_work
}

// Generic VirtIO RNG Device Driver Implementation
// Transport-agnostic RNG driver

#include "virtio_rng.h"
#include "kapi.h"
#include "kernel.h"
#include "printk.h"

// Initialize RNG with MMIO transport
int virtio_rng_init_mmio(virtio_rng_dev_t *rng, virtio_mmio_transport_t *mmio,
                         void *queue_memory, kernel_t *kernel) {
  rng->transport = mmio;
  rng->transport_type = VIRTIO_TRANSPORT_MMIO;
  rng->kernel = kernel;

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
    platform_memory_barrier();
  }

  // Feature negotiation (RNG needs no features)
  virtio_mmio_set_features(mmio, 0, 0);

  // Features OK
  uint8_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK;
  virtio_mmio_set_status(mmio, status);

  // Verify features OK
  if (!(virtio_mmio_get_status(mmio) & VIRTIO_STATUS_FEATURES_OK)) {
    return -1;
  }

  // Setup queue
  rng->vq_memory = queue_memory;
  rng->queue_size = virtio_mmio_get_queue_size(mmio, 0);

  if (rng->queue_size > VIRTIO_RNG_MAX_REQUESTS) {
    rng->queue_size = VIRTIO_RNG_MAX_REQUESTS;
  }

  virtqueue_init(&rng->vq, rng->queue_size, queue_memory);
  virtio_mmio_setup_queue(mmio, 0, &rng->vq, rng->queue_size);

  // Driver OK
  status |= VIRTIO_STATUS_DRIVER_OK;
  virtio_mmio_set_status(mmio, status);

  // Check if device failed
  if (virtio_mmio_get_status(mmio) & 0x80) { // VIRTIO_STATUS_FAILED
    return -1;
  }

  // Clear request tracking and irq_pending
  rng->irq_pending = 0;
  for (int i = 0; i < VIRTIO_RNG_MAX_REQUESTS; i++) {
    rng->active_requests[i] = (void *)0;
  }

  return 0;
}

// Initialize RNG with PCI transport
int virtio_rng_init_pci(virtio_rng_dev_t *rng, virtio_pci_transport_t *pci,
                        void *queue_memory, kernel_t *kernel) {
  rng->transport = pci;
  rng->transport_type = VIRTIO_TRANSPORT_PCI;
  rng->kernel = kernel;

  // Reset
  virtio_pci_reset(pci);

  // Acknowledge
  virtio_pci_set_status(pci, VIRTIO_STATUS_ACKNOWLEDGE);

  // Driver
  virtio_pci_set_status(pci, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

  // Feature negotiation (RNG needs no features)
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
  // Direct write to avoid unaligned pointer warning
  volatile virtio_pci_common_cfg_t *common_cfg = pci->common_cfg;
  common_cfg->msix_config = 0xFFFF;
  platform_memory_barrier();

  // Setup queue
  rng->vq_memory = queue_memory;
  rng->queue_size = virtio_pci_get_queue_size(pci, 0);
  if (rng->queue_size > VIRTIO_RNG_MAX_REQUESTS) {
    rng->queue_size = VIRTIO_RNG_MAX_REQUESTS;
  }

  virtqueue_init(&rng->vq, rng->queue_size, queue_memory);
  virtio_pci_setup_queue(pci, 0, &rng->vq, rng->queue_size);

  // Driver OK
  status |= VIRTIO_STATUS_DRIVER_OK;
  virtio_pci_set_status(pci, status);

  // Clear request tracking and irq_pending
  rng->irq_pending = 0;
  for (int i = 0; i < VIRTIO_RNG_MAX_REQUESTS; i++) {
    rng->active_requests[i] = (void *)0;
  }

  return 0;
}

// Submit work (transport-agnostic)
void virtio_rng_submit_work(virtio_rng_dev_t *rng, kwork_t *submissions,
                            kernel_t *k) {
  int submitted = 0;

  kwork_t *work = submissions;
  while (work != (void *)0) {
    kwork_t *next = work->next;

    if (work->op == KWORK_OP_RNG_READ) {
      krng_req_t *req = CONTAINER_OF(work, krng_req_t, work);

      // Allocate descriptor
      uint16_t desc_idx = virtqueue_alloc_desc(&rng->vq);

      if (desc_idx == VIRTQUEUE_NO_DESC) {
        // Queue full - immediate failure with backpressure
        kplatform_complete_work(k, work, KERR_BUSY);
        work = next;
        continue;
      }

      // Setup descriptor (device-writable buffer)
      virtqueue_add_desc(&rng->vq, desc_idx, (uint64_t)req->buffer, req->length,
                         VIRTQ_DESC_F_WRITE);

      // Add to available ring
      virtqueue_add_avail(&rng->vq, desc_idx);

      // Track request for completion (constant-time lookup)
      req->platform.desc_idx = desc_idx;
      rng->active_requests[desc_idx] = req;

      // Mark as live
      work->state = KWORK_STATE_LIVE;
      submitted++;
    }

    work = next;
  }

  // Kick device once for all descriptors (bulk submission)
  if (submitted > 0) {
    // Clean cache (ARM64 will flush, x64 is no-op)
    size_t desc_size = rng->queue_size * sizeof(virtq_desc_t);
    size_t avail_size = 4 + rng->queue_size * 2 + 2;
    platform_cache_clean(rng->vq.desc, desc_size);
    platform_cache_clean(rng->vq.avail, avail_size);

    // Notify device (transport-specific implementation)
    if (rng->transport_type == VIRTIO_TRANSPORT_MMIO) {
      virtio_mmio_notify_queue((virtio_mmio_transport_t *)rng->transport, 0);

      // Poll for completion (MMIO RNG is fast, no need to wait for interrupt)
      size_t used_size = 4 + rng->queue_size * sizeof(virtq_used_elem_t) + 2;

      for (int poll_iter = 0; poll_iter < 1000; poll_iter++) {
        // Invalidate used ring cache before checking
        platform_cache_invalidate(rng->vq.used, used_size);

        // Check if device has completed any work
        if (virtqueue_has_used(&rng->vq)) {
          // Process all available completions
          while (virtqueue_has_used(&rng->vq)) {
            uint16_t desc_idx;
            uint32_t len;
            virtqueue_get_used(&rng->vq, &desc_idx, &len);

            krng_req_t *req = rng->active_requests[desc_idx];
            if (req != (void *)0) {
              // Invalidate buffer cache
              platform_cache_invalidate(req->buffer, req->length);

              req->completed = len;
              kplatform_complete_work(k, &req->work, KERR_OK);
              rng->active_requests[desc_idx] = (void *)0;
            }

            virtqueue_free_desc(&rng->vq, desc_idx);
          }

          // Signal completion to wake up kernel (acts like interrupt)
          rng->irq_pending = 1;
          break; // All completions processed
        }
      }
    } else if (rng->transport_type == VIRTIO_TRANSPORT_PCI) {
      virtio_pci_notify_queue((virtio_pci_transport_t *)rng->transport, 0);
    }
  }
}

// Process interrupt (transport-agnostic)
void virtio_rng_process_irq(virtio_rng_dev_t *rng, kernel_t *k) {
  if (!rng->irq_pending) {
    return;
  }
  rng->irq_pending = 0;

  // Invalidate used ring cache (ARM64 will invalidate, x64 is no-op)
  size_t used_size = 4 + rng->queue_size * sizeof(virtq_used_elem_t) + 2;
  platform_cache_invalidate(rng->vq.used, used_size);

  while (virtqueue_has_used(&rng->vq)) {
    uint16_t desc_idx;
    uint32_t len;
    virtqueue_get_used(&rng->vq, &desc_idx, &len);

    krng_req_t *req = rng->active_requests[desc_idx];
    if (req != (void *)0) {
      // Invalidate buffer cache
      platform_cache_invalidate(req->buffer, req->length);

      req->completed = len;
      kplatform_complete_work(k, &req->work, KERR_OK);
      rng->active_requests[desc_idx] = (void *)0;
    }

    virtqueue_free_desc(&rng->vq, desc_idx);
  }
}

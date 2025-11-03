// Generic VirtIO RNG Device Driver Implementation
// Transport-agnostic RNG driver

#include "virtio_rng.h"
#include "irq_ring.h"
#include "kapi.h"
#include "kernel.h"
#include "printk.h"

// Initialize RNG with MMIO transport
int virtio_rng_init_mmio(virtio_rng_dev_t *rng, virtio_mmio_transport_t *mmio,
                         virtqueue_memory_t *queue_memory, kernel_t *kernel) {
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
    // Write GuestPageSize register (offset 0x028) with proper memory barrier
    platform_mmio_write32((volatile uint32_t *)(void *)(mmio->base + 0x028),
                          4096);
  }

  // Feature negotiation (RNG needs no features)
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
    printk("      FAILED: device failed\n");
    return -1;
  }

  // Clear request tracking and outstanding counter
  rng->outstanding_requests = 0;
  for (int i = 0; i < VIRTIO_RNG_MAX_REQUESTS; i++) {
    rng->active_requests[i] = NULL;
  }

  return 0;
}

// Initialize RNG with PCI transport
int virtio_rng_init_pci(virtio_rng_dev_t *rng, virtio_pci_transport_t *pci,
                        virtqueue_memory_t *queue_memory, kernel_t *kernel) {
  printk("[RNG] Initializing PCI transport...\n");
  rng->transport = pci;
  rng->transport_type = VIRTIO_TRANSPORT_PCI;
  rng->kernel = kernel;

  // Reset
  virtio_pci_reset(pci);
  printk("[RNG] Device reset\n");

  // Acknowledge
  virtio_pci_set_status(pci, VIRTIO_STATUS_ACKNOWLEDGE);
  printk("[RNG] ACKNOWLEDGE status set\n");

  // Driver
  virtio_pci_set_status(pci, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
  printk("[RNG] DRIVER status set\n");

  // Feature negotiation (RNG needs no features)
  virtio_pci_set_features(pci, 0, 0);
  printk("[RNG] Features negotiated\n");

  // Features OK
  uint8_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK;
  virtio_pci_set_status(pci, status);
  printk("[RNG] FEATURES_OK status set\n");

  // Verify features OK
  uint8_t actual_status = virtio_pci_get_status(pci);
  printk("[RNG] Device status readback: 0x");
  printk_hex8(actual_status);
  printk("\n");
  if (!(actual_status & VIRTIO_STATUS_FEATURES_OK)) {
    printk("[RNG] ERROR: Device rejected FEATURES_OK (status=0x");
    printk_hex8(actual_status);
    printk(")\n");
    return -1;
  }
  printk("[RNG] FEATURES_OK verified\n");

  // Write MSI-X config vector to device (if configured by platform code)
  // This must be done AFTER reset but BEFORE DRIVER_OK
  platform_mmio_write16(&pci->common_cfg->msix_config, pci->msix_config_vector);
  printk("[RNG] MSI-X config vector written: 0x");
  printk_hex16(pci->msix_config_vector);
  printk("\n");

  // Setup queue
  rng->vq_memory = queue_memory;
  rng->queue_size = virtio_pci_get_queue_size(pci, 0);
  if (rng->queue_size > VIRTIO_RNG_MAX_REQUESTS) {
    rng->queue_size = VIRTIO_RNG_MAX_REQUESTS;
  }
  printk("[RNG] Queue size: ");
  printk_dec(rng->queue_size);
  printk("\n");

  virtqueue_init(&rng->vq, rng->queue_size, queue_memory);
  printk("[RNG] Virtqueue initialized\n");
  virtio_pci_setup_queue(pci, 0, &rng->vq, rng->queue_size);
  printk("[RNG] Queue setup complete\n");

  // Driver OK
  status |= VIRTIO_STATUS_DRIVER_OK;
  virtio_pci_set_status(pci, status);
  printk("[RNG] DRIVER_OK status set\n");

  // Clear request tracking and outstanding counter
  rng->outstanding_requests = 0;
  for (int i = 0; i < VIRTIO_RNG_MAX_REQUESTS; i++) {
    rng->active_requests[i] = NULL;
  }

  printk("[RNG] Initialization complete\n");
  return 0;
}

// Submit work (transport-agnostic)
void virtio_rng_submit_work(virtio_rng_dev_t *rng, kwork_t *submissions,
                            kernel_t *k) {
  int submitted = 0;

  kwork_t *work = submissions;
  while (work != NULL) {
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
      rng->outstanding_requests++;
      submitted++;
    }

    work = next;
  }

  // Kick device once for all descriptors (bulk submission)
  if (submitted > 0) {
    // Memory barrier before notify
    __sync_synchronize();

    // Notify device (transport-specific implementation)
    if (rng->transport_type == VIRTIO_TRANSPORT_MMIO) {
      virtio_mmio_notify_queue((virtio_mmio_transport_t *)rng->transport, 0);
    } else if (rng->transport_type == VIRTIO_TRANSPORT_PCI) {
      virtio_pci_notify_queue((virtio_pci_transport_t *)rng->transport,
                              &rng->vq);
    }

    // Enqueue device for polling on next tick
    // This is critical for MMIO devices which may not generate interrupts
    // reliably The device will be checked in platform_tick and re-enqueued if
    // work remains
    kirq_ring_enqueue(&((platform_t *)rng->base.platform)->irq_ring, rng);
  }
}

// Process interrupt (transport-agnostic)
void virtio_rng_process_irq(virtio_rng_dev_t *rng, kernel_t *k) {
  // Process all available completions
  while (virtqueue_has_used(&rng->vq)) {
    uint16_t desc_idx;
    uint32_t len;
    virtqueue_get_used(&rng->vq, &desc_idx, &len);

    krng_req_t *req = rng->active_requests[desc_idx];
    if (req != NULL) {
      req->completed = len;
      kplatform_complete_work(k, &req->work, KERR_OK);
      rng->active_requests[desc_idx] = NULL;
      rng->outstanding_requests--;
    }

    virtqueue_free_desc(&rng->vq, desc_idx);
  }

  // Re-enqueue if there are still outstanding requests (work not yet completed)
  // This keeps the device in the polling loop until all work is done
  // Critical for MMIO devices that don't reliably generate interrupts
  if (rng->outstanding_requests > 0) {
    kirq_ring_enqueue(&((platform_t *)rng->base.platform)->irq_ring, rng);
  }
}

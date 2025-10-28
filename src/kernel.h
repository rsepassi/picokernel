// vmos Async Work Queue Kernel
// Core kernel structures and internal API

#pragma once

#include "kapi.h"

// Kernel state
struct kernel {
  platform_t platform;

  // Work queues
  kwork_t *submit_queue_head; // Doubly-linked: pending submission
  kwork_t *submit_queue_tail;
  kwork_t *cancel_queue_head; // Singly-linked: pending cancellation
  kwork_t *ready_queue_head;  // Singly-linked: ready for callback

  // Timer management
  kwork_t *timer_list_head; // Doubly-linked: active timers
  kwork_t *timer_list_tail;
  uint64_t current_time_ms;
};

// Internal Kernel API

// Kernel main
void kmain(void *fdt);

// Initialize kernel
void kinit(kernel_t *k, void *fdt);

// Get next timeout for platform_wfi
uint64_t knext_delay(kernel_t *k);

// Process kernel tick (expire timers, run callbacks, submit work)
void ktick(kernel_t *k, uint64_t current_time);

// Platform → Kernel Interface (called by platform code)

// Mark work as complete (moves LIVE -> READY)
void kplatform_complete_work(kernel_t *k, kwork_t *work, kerr_t result);

// Mark cancellation as complete (best-effort)
void kplatform_cancel_work(kernel_t *k, kwork_t *work);

// Platform-Specific Functions (implemented by platform code)

// Process deferred interrupt work (called from ktick before callbacks)
void kplatform_tick(platform_t *platform, kernel_t *k);

// Submit work and cancellations to platform (called from ktick)
// submissions: singly-linked list of work to submit (or NULL)
// cancellations: singly-linked list of work to cancel (or NULL)
void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations);

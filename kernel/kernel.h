// vmos Async Work Queue Kernel
// Core kernel structures and internal API

#pragma once

#include "kapi.h"

// Kernel state
struct kernel {
  struct platform platform;

  // Work queues
  kwork_t *submit_queue_head; // Doubly-linked: pending submission
  kwork_t *submit_queue_tail;
  kwork_t *cancel_queue_head; // Singly-linked: pending cancellation
  kwork_t *ready_queue_head;  // Singly-linked: ready for callback

  // Timer management
  ktimer_req_t *timer_heap_root; // Root of min-heap tree
  size_t timer_heap_size;        // Number of active timers
  ktime_t current_time_ns;       // Current monotonic time in nanoseconds

#ifdef KDEBUG
  // Work transition history (debug builds only)
  struct {
    kwork_t *work;
    uint8_t from_state;
    uint8_t to_state;
    ktime_t timestamp_ns; // Timestamp in nanoseconds
  } work_history[16];
  uint32_t work_history_idx;
#endif
};

// Internal Kernel API

// Kernel main
void kmain(void *fdt);

// Get global kernel pointer (FOR LOGGING/DEBUG ONLY - do not use elsewhere)
kernel_t *kget_kernel__logonly__(void);

// Get current time in nanoseconds (FOR LOGGING/DEBUG ONLY - do not use elsewhere)
ktime_t kget_time_ns__logonly__(void);

// Initialize kernel
void kmain_init(kernel_t *k, void *fdt);

// Get next timeout for platform_wfi (in nanoseconds)
ktime_t kmain_next_delay(kernel_t *k);

// Process kernel tick (expire timers, run callbacks, submit work)
void kmain_tick(kernel_t *k, ktime_t current_time);

// Platform → Kernel Interface (called by platform code)

// Mark work as complete (moves LIVE -> READY)
void kplatform_complete_work(kernel_t *k, kwork_t *work, kerr_t result);

// Mark cancellation as complete (best-effort)
void kplatform_cancel_work(kernel_t *k, kwork_t *work);

// Platform-Specific Functions (implemented by platform code)

// Process deferred interrupt work (called from ktick before callbacks)
void kplatform_tick(platform_t *platform, kernel_t *k);

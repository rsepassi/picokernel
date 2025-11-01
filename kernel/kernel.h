// vmos Async Work Queue Kernel
// Core kernel structures and internal API

#pragma once

#include "kapi.h"
#include "kcsprng.h"

// CSPRNG initialization state (stack-allocated by caller)
typedef struct {
  uint8_t seed_buffer[64];
  krng_req_t seed_req;
  volatile uint8_t seed_ready;
} kcsprng_init_state_t;

typedef uint64_t ktime_t;

// Kernel state
struct kernel {
  platform_t platform;

  // Work queues
  kwork_t *submit_queue_head; // Doubly-linked: pending submission
  kwork_t *submit_queue_tail;
  kwork_t *cancel_queue_head; // Singly-linked: pending cancellation
  kwork_t *ready_queue_head;  // Singly-linked: ready for callback

  // Timer management
  ktimer_req_t *timer_heap_root; // Root of min-heap tree
  size_t timer_heap_size;        // Number of active timers
  ktime_t current_time_ms;

  kcsprng_ctx rng;

#ifdef KDEBUG
  // Work transition history (debug builds only)
  struct {
    kwork_t *work;
    uint8_t from_state;
    uint8_t to_state;
    uint64_t timestamp_ms;
  } work_history[16];
  uint32_t work_history_idx;
#endif
};

// Internal Kernel API

// Kernel main
void kmain(void *fdt);

// Get global kernel pointer (FOR LOGGING/DEBUG ONLY - do not use elsewhere)
kernel_t *kget_kernel__logonly__(void);

// Initialize kernel
void kmain_init(kernel_t *k, void *fdt);

// Initialize CSPRNG with strong entropy from virtio-rng
void kmain_init_csprng(kernel_t *k, kcsprng_init_state_t *state);

// Get next timeout for platform_wfi
uint64_t kmain_next_delay(kernel_t *k);

// Process kernel tick (expire timers, run callbacks, submit work)
void kmain_tick(kernel_t *k, uint64_t current_time);

// ktick + platform_wfi
void kmain_step(kernel_t *k, uint64_t max_timeout);

// Platform → Kernel Interface (called by platform code)

// Mark work as complete (moves LIVE -> READY)
void kplatform_complete_work(kernel_t *k, kwork_t *work, kerr_t result);

// Mark cancellation as complete (best-effort)
void kplatform_cancel_work(kernel_t *k, kwork_t *work);

// Platform-Specific Functions (implemented by platform code)

// Process deferred interrupt work (called from ktick before callbacks)
void kplatform_tick(platform_t *platform, kernel_t *k);

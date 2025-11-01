// vmos Async Work Queue Kernel Implementation

#include "kernel.h"
#include "printk.h"
#include "timer_heap.h"

// Work queue debugging (KDEBUG only)
#ifdef KDEBUG

static void record_work_transition(kernel_t *k, kwork_t *work,
                                   kwork_state_t from_state,
                                   kwork_state_t to_state) {
  if (k == NULL || work == NULL) {
    return;
  }

  uint32_t idx = k->work_history_idx;
  k->work_history[idx].work = work;
  k->work_history[idx].from_state = (uint8_t)from_state;
  k->work_history[idx].to_state = (uint8_t)to_state;
  k->work_history[idx].timestamp_ms = k->current_time_ms;
  k->work_history_idx = (idx + 1) % 16;
}

void kdebug_dump_work_history(void) {
  kernel_t *k = kget_kernel__logonly__();
  if (k == NULL) {
    printk("\nNo work history (kernel not initialized)\n");
    return;
  }

  printk("\nLast work transitions:\n");

  const char *state_names[] = {
      "DEAD", "SUBMIT_REQUESTED", "LIVE", "CANCEL_REQUESTED", "READY"};

  for (uint32_t i = 0; i < 16; i++) {
    if (k->work_history[i].work == NULL) {
      continue; // Empty slot
    }

    printk("  ");
    printk_hex64((uint64_t)k->work_history[i].work);
    printk(": ");

    uint8_t from = k->work_history[i].from_state;
    uint8_t to = k->work_history[i].to_state;

    if (from < 5) {
      printk(state_names[from]);
    } else {
      printk("UNKNOWN");
    }

    printk(" -> ");

    if (to < 5) {
      printk(state_names[to]);
    } else {
      printk("UNKNOWN");
    }

    printk(" @ ");
    printk_dec(k->work_history[i].timestamp_ms);
    printk("ms\n");
  }
}

#else
// Release build: no-op
static inline void record_work_transition(kernel_t *k, kwork_t *work,
                                          kwork_state_t from_state,
                                          kwork_state_t to_state) {
  (void)k;
  (void)work;
  (void)from_state;
  (void)to_state;
}
#endif

// Run event loop until a condition is true or a maximum timeout is reached
#define KWAIT_UNTIL(kernel, cond, step_timeout, max_timeout)                   \
  do {                                                                         \
    kernel_t *_k = (kernel);                                                   \
    ktime_t _max_timeout = (max_timeout);                                      \
    ktime_t _step_timeout = (step_timeout);                                    \
    ktime_t start_time = _k->current_time_ms;                                  \
    while (!(cond) && (_k->current_time_ms - start_time) < (_max_timeout)) {   \
      kmain_step(_k, _step_timeout);                                           \
    }                                                                          \
  } while (0)

// Queue management helpers

static void enqueue_submit(kernel_t *k, kwork_t *work) {
  work->next = NULL;
  work->prev = k->submit_queue_tail;

  if (k->submit_queue_tail != NULL) {
    k->submit_queue_tail->next = work;
  } else {
    k->submit_queue_head = work;
  }
  k->submit_queue_tail = work;
}

static void enqueue_cancel(kernel_t *k, kwork_t *work) {
  work->next = k->cancel_queue_head;
  work->prev = NULL;
  k->cancel_queue_head = work;
}

static void enqueue_ready(kernel_t *k, kwork_t *work) {
  work->next = k->ready_queue_head;
  work->prev = NULL;
  k->ready_queue_head = work;
}

static void enqueue_timer(kernel_t *k, kwork_t *work) {
  ktimer_req_t *timer = CONTAINER_OF(work, ktimer_req_t, work);
  timer_heap_insert(k, timer);
}

// Expire timers and move to ready queue
static void expire_timers(kernel_t *k) {
  // Repeatedly extract minimum timer while it's expired
  while (1) {
    ktimer_req_t *timer = timer_heap_peek_min(k);

    if (timer == NULL || timer->deadline_ms > k->current_time_ms) {
      break; // No more expired timers
    }

    // Timer expired - extract from heap
    timer_heap_extract_min(k);

    // Move to ready queue
    timer->work.result = KERR_OK;
    record_work_transition(k, &timer->work, timer->work.state,
                           KWORK_STATE_READY);
    timer->work.state = KWORK_STATE_READY;
    enqueue_ready(k, &timer->work);
  }
}

// Initialize kernel
void kmain_init(kernel_t *k, void *fdt) {
  *k = (kernel_t){0};

  // Initialize platform
  platform_init(&k->platform, fdt, k);

  // Initialize start time
  k->current_time_ms = platform_wfi(&k->platform, 0);

  // Enable interrupts
  platform_interrupt_enable(&k->platform);
  KLOG("interrupts enabled");

  // Initialize CSPRNG with strong entropy
  kcsprng_init_state_t csprng_init_state;
  kmain_init_csprng(k, &csprng_init_state);
  KLOG("CSPRNG ready");

  printk("kmain_init complete\n");
}

// Submit work item
kerr_t ksubmit(kernel_t *k, kwork_t *work) {
  if (work == NULL || work->callback == NULL) {
    return KERR_INVALID;
  }

  if (work->state != KWORK_STATE_DEAD) {
    return KERR_BUSY;
  }

  // Queue for submission
  record_work_transition(k, work, work->state, KWORK_STATE_SUBMIT_REQUESTED);
  work->state = KWORK_STATE_SUBMIT_REQUESTED;
  work->result = KERR_OK;

  // Add to appropriate queue based on operation type
  if (work->op == KWORK_OP_TIMER) {
    // Timers go directly to timer list
    enqueue_timer(k, work);
    record_work_transition(k, work, work->state, KWORK_STATE_LIVE);
    work->state = KWORK_STATE_LIVE;
  } else {
    // Other operations go to submit queue for platform
    enqueue_submit(k, work);
  }

  return KERR_OK;
}

// Request cancellation
kerr_t kcancel(kernel_t *k, kwork_t *work) {
  if (work == NULL) {
    return KERR_INVALID;
  }

  if (work->state == KWORK_STATE_DEAD || work->state == KWORK_STATE_READY) {
    return KERR_INVALID;
  }

  if (work->state == KWORK_STATE_LIVE) {
    // Move to cancel queue
    record_work_transition(k, work, work->state, KWORK_STATE_CANCEL_REQUESTED);
    work->state = KWORK_STATE_CANCEL_REQUESTED;
    enqueue_cancel(k, work);
  }

  return KERR_OK;
}

// Release a receive buffer back to the ring (for standing work)
void knet_buffer_release(kernel_t *k, knet_recv_req_t *req,
                         size_t buffer_index) {
  if (k == NULL || req == NULL) {
    return;
  }

  // Validate buffer index
  if (buffer_index >= req->num_buffers) {
    return;
  }

  // Call platform layer to handle buffer release
  platform_net_buffer_release(&k->platform, req, buffer_index);
}

// Initialize work item
void kwork_init(kwork_t *work, uint32_t op, void *ctx,
                kwork_callback_t callback, uint8_t flags) {
  work->op = op;
  work->callback = callback;
  work->ctx = ctx;
  work->result = KERR_OK;
  // Note: No transition recording for init - kernel pointer not available here
  work->state = KWORK_STATE_DEAD;
  work->flags = flags;
  work->next = NULL;
  work->prev = NULL;
}

// Get next timeout for platform_wfi
uint64_t kmain_next_delay(kernel_t *k) {
  ktimer_req_t *timer = timer_heap_peek_min(k);

  if (timer == NULL) {
    return UINT64_MAX; // No timers, wait forever
  }

  if (timer->deadline_ms <= k->current_time_ms) {
    return 0; // Already expired
  }

  return timer->deadline_ms - k->current_time_ms;
}

// Process kernel tick
void kmain_tick(kernel_t *k, uint64_t current_time) {
  // Update kernel time
  k->current_time_ms = current_time;

  // Expire timers
  expire_timers(k);

  // Process deferred interrupt work (moves LIVE work to READY)
  platform_tick(&k->platform, k);

  // Run all ready callbacks
  kwork_t *work = k->ready_queue_head;
  while (work != NULL) {
    kwork_t *next = work->next;

    // Remove from ready queue
    k->ready_queue_head = next;

    // Standing work stays LIVE if successful, otherwise goes DEAD
    if ((work->flags & KWORK_FLAG_STANDING) && work->result == KERR_OK) {
      record_work_transition(k, work, work->state, KWORK_STATE_LIVE);
      work->state = KWORK_STATE_LIVE;
    } else {
      record_work_transition(k, work, work->state, KWORK_STATE_DEAD);
      work->state = KWORK_STATE_DEAD;
    }

    // Invoke callback
    work->callback(work);

    work = next;
  }

  // Process timer cancellations (timers are managed in kernel, not platform)
  kwork_t *cancel = k->cancel_queue_head;
  kwork_t *platform_cancel_head = NULL;
  while (cancel != NULL) {
    kwork_t *next = cancel->next;

    if (cancel->op == KWORK_OP_TIMER) {
      // Remove timer from heap
      ktimer_req_t *timer = CONTAINER_OF(cancel, ktimer_req_t, work);
      timer_heap_delete(k, timer);

      // Mark as cancelled
      cancel->result = KERR_CANCELLED;
      record_work_transition(k, cancel, cancel->state, KWORK_STATE_READY);
      cancel->state = KWORK_STATE_READY;
      enqueue_ready(k, cancel);
    } else {
      // Keep non-timer cancellations for platform
      cancel->next = platform_cancel_head;
      platform_cancel_head = cancel;
    }

    cancel = next;
  }

  // Submit work and non-timer cancellations to platform (bulk)
  if (k->submit_queue_head != NULL || platform_cancel_head != NULL) {
    platform_submit(&k->platform, k->submit_queue_head, platform_cancel_head);
    k->submit_queue_head = k->submit_queue_tail = NULL;
  }
  k->cancel_queue_head = NULL;
}

// Platform → Kernel: Mark work as complete
void kplatform_complete_work(kernel_t *k, kwork_t *work, kerr_t result) {
  if (work == NULL) {
    return;
  }

  work->result = result;
  record_work_transition(k, work, work->state, KWORK_STATE_READY);
  work->state = KWORK_STATE_READY;
  enqueue_ready(k, work);
}

// Platform → Kernel: Mark cancellation as complete
void kplatform_cancel_work(kernel_t *k, kwork_t *work) {
  if (work == NULL) {
    return;
  }

  work->result = KERR_CANCELLED;
  record_work_transition(k, work, work->state, KWORK_STATE_READY);
  work->state = KWORK_STATE_READY;
  enqueue_ready(k, work);
}

// CSPRNG initialization
static void csprng_seed_callback(kwork_t *work) {
  if (work->result != KERR_OK) {
    printk("ERROR: Failed to get entropy for CSPRNG: error ");
    printk_dec(work->result);
    printk("\n");
    return;
  }

  krng_req_t *req = CONTAINER_OF(work, krng_req_t, work);
  printk("Got ");
  printk_dec(req->completed);
  printk(" bytes of entropy from virtio-rng\n");

  // Mark seed as ready (state is in ctx)
  kcsprng_init_state_t *state = (kcsprng_init_state_t *)work->ctx;
  state->seed_ready = 1;
}

void kmain_init_csprng(kernel_t *k, kcsprng_init_state_t *state) {
  printk("Initializing CSPRNG with virtio-rng entropy...\n");

  state->seed_ready = 0;

  // Setup RNG request for entropy
  kwork_init(&state->seed_req.work, KWORK_OP_RNG_READ, state,
             csprng_seed_callback, 0);
  state->seed_req.buffer = state->seed_buffer;
  state->seed_req.length = 64;
  state->seed_req.completed = 0;

  // Submit request
  kerr_t err = ksubmit(k, &state->seed_req.work);
  KASSERT(err == KERR_OK, "CSPRNG submit failed");

  // Wait for entropy by running event loop until seed is ready
  printk("Waiting for entropy...\n");
  KWAIT_UNTIL(k, state->seed_ready, 10, 100);

  KASSERT(state->seed_ready, "CSPRNG init failed");

  // Initialize CSPRNG with entropy
  kcsprng_init(&k->rng, state->seed_buffer, sizeof(state->seed_buffer));

  printk("[CSPRNG] CSPRNG initialized\n");
}

void kmain_step(kernel_t *k, uint64_t max_timeout) {
  KLOG("[KLOOP] tick");
  kmain_tick(k, k->current_time_ms);
  uint64_t timeout = kmain_next_delay(k);
  if (timeout > max_timeout)
    timeout = max_timeout;
  KLOG("[KLOOP] wfi");
  k->current_time_ms = platform_wfi(&k->platform, timeout);
}

// vmos Async Work Queue Kernel Implementation

#include "kernel.h"
#include "printk.h"

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
  work->next = NULL;
  work->prev = k->timer_list_tail;

  if (k->timer_list_tail != NULL) {
    k->timer_list_tail->next = work;
  } else {
    k->timer_list_head = work;
  }
  k->timer_list_tail = work;
}

static void dequeue_timer(kernel_t *k, kwork_t *work) {
  if (work->prev != NULL) {
    work->prev->next = work->next;
  } else {
    k->timer_list_head = work->next;
  }

  if (work->next != NULL) {
    work->next->prev = work->prev;
  } else {
    k->timer_list_tail = work->prev;
  }

  work->next = NULL;
  work->prev = NULL;
}

// Expire timers and move to ready queue
static void expire_timers(kernel_t *k) {
  kwork_t *timer = k->timer_list_head;

  while (timer != NULL) {
    kwork_t *next = timer->next;

    ktimer_req_t *req = CONTAINER_OF(timer, ktimer_req_t, work);

    if (req->deadline_ms <= k->current_time_ms) {
      // Timer expired - remove from timer list
      dequeue_timer(k, timer);

      // Move to ready queue
      timer->result = KERR_OK;
      timer->state = KWORK_STATE_READY;
      enqueue_ready(k, timer);
    }

    timer = next;
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
  work->state = KWORK_STATE_SUBMIT_REQUESTED;
  work->result = KERR_OK;

  // Add to appropriate queue based on operation type
  if (work->op == KWORK_OP_TIMER) {
    // Timers go directly to timer list
    enqueue_timer(k, work);
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
    work->state = KWORK_STATE_CANCEL_REQUESTED;
    enqueue_cancel(k, work);
  }

  return KERR_OK;
}

// Release a receive buffer back to the ring (for standing work)
void knet_buffer_release(kernel_t *k, knet_recv_req_t *req, size_t buffer_index) {
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

// Get next timeout for platform_wfi
uint64_t kmain_next_delay(kernel_t *k) {
  if (k->timer_list_head == NULL) {
    return UINT64_MAX; // No timers, wait forever
  }

  // Scan for earliest timer (O(n) - will optimize to heap/wheel later)
  uint64_t min_delay = UINT64_MAX;
  kwork_t *timer = k->timer_list_head;

  while (timer != NULL) {
    ktimer_req_t *req = CONTAINER_OF(timer, ktimer_req_t, work);

    if (req->deadline_ms <= k->current_time_ms) {
      return 0; // Already expired
    }

    uint64_t delay = req->deadline_ms - k->current_time_ms;
    if (delay < min_delay) {
      min_delay = delay;
    }

    timer = timer->next;
  }

  return min_delay;
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
      work->state = KWORK_STATE_LIVE;
    } else {
      work->state = KWORK_STATE_DEAD;
    }

    // Invoke callback
    work->callback(work);

    work = next;
  }

  // Submit work and cancellations to platform (bulk)
  if (k->submit_queue_head != NULL || k->cancel_queue_head != NULL) {
    platform_submit(&k->platform, k->submit_queue_head, k->cancel_queue_head);
    k->submit_queue_head = k->submit_queue_tail = NULL;
    k->cancel_queue_head = NULL;
  }
}

// Platform → Kernel: Mark work as complete
void kplatform_complete_work(kernel_t *k, kwork_t *work, kerr_t result) {
  if (work == NULL) {
    return;
  }

  work->result = result;
  work->state = KWORK_STATE_READY;
  enqueue_ready(k, work);
}

// Platform → Kernel: Mark cancellation as complete
void kplatform_cancel_work(kernel_t *k, kwork_t *work) {
  if (work == NULL) {
    return;
  }

  work->result = KERR_CANCELLED;
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
  state->seed_req.work.op = KWORK_OP_RNG_READ;
  state->seed_req.work.callback = csprng_seed_callback;
  state->seed_req.work.ctx = state;
  state->seed_req.work.state = KWORK_STATE_DEAD;
  state->seed_req.work.flags = 0;
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

  printk("CSPRNG initialized\n");
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

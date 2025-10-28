// vmos Async Work Queue Kernel Implementation

#include "kernel.h"
#include "printk.h"
#include <stddef.h>

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
void kinit(kernel_t *k, void *fdt) {
  // Set platform backpointer to kernel BEFORE platform_init
  // (platform_init may register interrupt handlers that need this)
  k->platform.kernel = k;

  printk("Initializing work queues...\n");
  // Initialize work queues
  k->submit_queue_head = NULL;
  k->submit_queue_tail = NULL;
  k->cancel_queue_head = NULL;
  k->ready_queue_head = NULL;

  printk("Initializing timer management...\n");
  // Initialize timer management
  k->timer_list_head = NULL;
  k->timer_list_tail = NULL;
  k->current_time_ms = 0;

  // Initialize platform
  platform_init(&k->platform, fdt);

  printk("kinit complete\n");
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

// Get next timeout for platform_wfi
uint64_t knext_delay(kernel_t *k) {
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
void ktick(kernel_t *k, uint64_t current_time) {
  // 1. Update kernel time
  k->current_time_ms = current_time;

  // 2. Expire timers
  expire_timers(k);

  // 3. Process deferred interrupt work (moves LIVE work to READY)
  kplatform_tick(&k->platform, k);

  // 4. Run all ready callbacks
  kwork_t *work = k->ready_queue_head;
  while (work != NULL) {
    kwork_t *next = work->next;

    // Remove from ready queue
    k->ready_queue_head = next;

    // Mark as dead (manual resubmit if needed)
    work->state = KWORK_STATE_DEAD;

    // Invoke callback
    work->callback(work);

    work = next;
  }

  // 5. Submit work and cancellations to platform (bulk)
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

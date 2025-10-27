# Async Architecture

## Overview

vmos uses a cooperative async work queue architecture for all asynchronous operations (timers, I/O, device operations). This document describes the core async subsystem.

## Core Concepts

### Work Items

All async operations are represented by a `kwork_t` structure. Work items are:
- **Caller-owned**: The caller allocates and maintains the kwork_t until the callback fires
- **Intrusive**: Embedded in operation-specific structures using CONTAINER_OF
- **Type-tagged**: Operation type determines how the work is processed
- **State-tracked**: Explicit state machine for lifecycle management

### Work Lifecycle

```
DEAD -> [ksubmit] -> SUBMIT_REQUESTED -> [ktick] -> LIVE -> [interrupt/timer] -> READY -> [ktick] -> DEAD
                                                                                              |
                                                                                             \/
                                                                                          [callback]
```

### Event Loop Model

The kernel runs a cooperative event loop:

```c
void main(void* fdt) {
    kernel_t k;
    kinit(&k, fdt);
    kusermain(&k);  // User submits initial work

    uint64_t current_time = 0;
    while (1) {
        ktick(&k, current_time);
        current_time = platform_wfi(&k.platform, knext_delay(&k));
    }
}
```

## Data Structures

### kwork_t (Core Work Item)

```c
typedef uint32_t kerr_t;

typedef enum {
    WORK_STATE_DEAD             = 0,  // Not initialized
    WORK_STATE_SUBMIT_REQUESTED = 1,  // Queued for submission
    WORK_STATE_LIVE             = 2,  // Submitted to platform/timer
    WORK_STATE_READY            = 3,  // Ready for callback
    WORK_STATE_CANCEL_REQUESTED = 4,  // Cancel requested
} kwork_state_t;

typedef void (*kwork_callback_t)(kwork_t* work);

struct work {
    uint32_t op;                // Operation type (KWORK_OP_*)
    kwork_callback_t callback;   // Completion callback
    void* ctx;                  // Caller context
    kerr_t result;              // Result code
    uint8_t state;              // work_state_t
    uint8_t flags;              // Reserved for future use
    kwork_t* next;               // Intrusive list
    kwork_t* prev;               // Intrusive list (submit/cancel queues only)
};
```

### Operation Types

```c
typedef enum {
    KWORK_OP_TIMER       = 1,  // Timer expiration
    KWORK_OP_RNG_READ    = 2,  // Random number generation
    // Future: WORK_OP_BLOCK_READ, WORK_OP_NET_SEND, etc.
} kwork_op_t;
```

### Error Codes

```c
#define KERR_OK             0  // Success
#define KERR_BUSY           1  // Resource busy
#define KERR_INVALID        2  // Invalid argument
#define KERR_CANCELLED      3  // Operation cancelled
#define KERR_TIMEOUT        4  // Timeout
#define KERR_NO_DEVICE      5  // Device not available
```

### kernel_t (Kernel State)

```c
typedef struct {
    platform_t platform;

    // Work queues
    kwork_t* submit_queue_head;  // Doubly-linked: pending submission
    kwork_t* submit_queue_tail;
    kwork_t* cancel_queue_head;  // Singly-linked: pending cancellation
    kwork_t* ready_queue_head;   // Singly-linked: ready for callback

    // Timer management
    kwork_t* timer_list_head;    // Doubly-linked: active timers
    kwork_t* timer_list_tail;
    uint64_t current_time_ms;
} kernel_t;
```

## API

### Public Kernel API

```c
// Initialize kernel
void kinit(kernel_t* k, void* fdt);

// Submit work item (queues for bulk submission)
kerr_t ksubmit(kernel_t* k, kwork_t* work);

// Request cancellation (best-effort)
kerr_t kcancel(kernel_t* k, kwork_t* work);

// Get next timeout for platform_wfi
uint64_t knext_delay(kernel_t* k);

// Process kernel tick (expire timers, run callbacks, submit work)
void ktick(kernel_t* k, uint64_t current_time);

// User entry point (called once after kinit)
void kusermain(kernel_t* k);
```

### Kernel ↔ Platform Interface

These functions are called by platform-specific code:

```c
// Mark work as complete (moves LIVE -> READY)
void kplatform_complete_work(kernel_t* k, kwork_t* work, kerr_t result);

// Mark cancellation as complete (best-effort)
void kplatform_cancel_work(kernel_t* k, kwork_t* work);
```

### Platform-Specific Functions

Platform code must implement:

```c
// Submit work and cancellations to platform (called from ktick)
// submissions: singly-linked list of work to submit (or NULL)
// cancellations: singly-linked list of work to cancel (or NULL)
void platform_submit(platform_t* platform, kwork_t* submissions, kwork_t* cancellations);
```

## ktick() Processing

The `ktick()` function is the heart of the event loop. It processes events and manages work items.

### Execution Order

1. **Update kernel time**: Store current_time from platform_wfi
2. **Expire timers**: Scan timer list, move expired timers to ready queue
3. **Run callbacks**: Execute all callbacks in ready queue
4. **Submit to platform**: Bulk submit work and cancellations to platform

### Detailed Flow

```c
void ktick(kernel_t* k, uint64_t current_time) {
    // 1. Update kernel time
    k->current_time_ms = current_time;

    // 2. Expire timers
    expire_timers(k);

    // 3. Run all ready callbacks
    kwork_t* work = k->ready_queue_head;
    while (work != NULL) {
        kwork_t* next = work->next;

        // Remove from ready queue
        dequeue_ready(k, work);

        // Mark as dead (manual resubmit if needed)
        work->state = WORK_STATE_DEAD;

        // Invoke callback
        work->callback(work);

        work = next;
    }

    // 4. Submit work and cancellations to platform (bulk)
    if (k->submit_queue_head != NULL || k->cancel_queue_head != NULL) {
        platform_submit(&k->platform, k->submit_queue_head, k->cancel_queue_head);
        k->submit_queue_head = k->submit_queue_tail = NULL;
        k->cancel_queue_head = NULL;
    }
}
```

## Interrupt Handling

### Interrupt Routing

Platform code maintains an IRQ routing table for constant-time dispatch:

```c
// platform/x64/interrupt.c

#define MAX_IRQ_VECTORS 256

typedef struct {
    void* context;
    void (*handler)(void* context);
} irq_entry_t;

static irq_entry_t g_irq_table[MAX_IRQ_VECTORS];

void irq_register(uint8_t vector, void (*handler)(void*), void* context);
void irq_dispatch(uint8_t vector);
```

### Interrupt Handler Flow

1. **Hardware interrupt** → CPU jumps to ISR
2. **ISR** identifies IRQ vector and calls `irq_dispatch(vector)`
3. **irq_dispatch** does constant-time lookup: `g_irq_table[vector].handler(context)`
4. **Device handler** processes interrupt:
   - Identifies completed work (e.g., via virtqueue used ring)
   - Calls `kplatform_complete_work(k, work, result)`
   - This moves work from LIVE → READY state
5. **CPU** returns from interrupt (IRET)
6. **platform_wfi()** wakes from HLT, reads current time, and returns it
7. **main loop** calls `ktick(current_time)` which runs ready callbacks

Note: **Any interrupt wakes platform_wfi()**. If no work is ready, ktick() completes quickly and we loop back to platform_wfi().

## Time Management

### Current Time

`platform_wfi()` returns the current time, which is then passed to `ktick()`:

```c
uint64_t platform_wfi(platform_t* platform, uint64_t timeout_ms) {
    // Set timeout timer if requested
    if (timeout_ms != UINT64_MAX) {
        timer_set_oneshot_ms(timeout_ms, timer_callback);
    }

    // Wait for any interrupt (STI + HLT)
    __asm__ volatile("sti; hlt");

    // Interrupt occurred, read and return current time
    return timer_get_current_time_ms();
}
```

`ktick()` stores this in `kernel_t::current_time_ms` for timer expiration calculations.

Note: We HLT once and return on **any interrupt**. Spurious interrupts just cause an extra (cheap) ktick() call.

### Timer Expiration

Timers are managed by the kernel:

1. **ksubmit()** with `WORK_OP_TIMER` inserts into timer list (sorted)
2. **knext_delay()** scans timer list for next expiration
3. **ktick()** expires timers and moves to ready queue
4. Only **one platform timer** is active: the `platform_wfi()` timeout

### Timer List (Temporary)

Currently a doubly-linked list, scanned in `knext_delay()`:

```c
uint64_t knext_delay(kernel_t* k) {
    if (k->timer_list_head == NULL) {
        return UINT64_MAX;  // No timers, wait forever
    }

    // Scan for earliest timer (O(n) - will optimize to heap/wheel later)
    uint64_t min_delay = UINT64_MAX;
    kwork_t* timer = k->timer_list_head;
    while (timer != NULL) {
        ktimer_req_t* req = CONTAINER_OF(timer, ktimer_req_t, work);
        uint64_t delay = req->deadline_ms - k->current_time_ms;
        if (delay < min_delay) {
            min_delay = delay;
        }
        timer = timer->next;
    }

    return min_delay;
}
```

Future: Replace with intrusive heap or timer wheel for O(1) amortized.

## Operation-Specific Structures

Each operation type embeds `kwork_t`:

```c
// Timer request
typedef struct {
    kwork_t work;            // Embedded work item
    uint64_t deadline_ms;   // Absolute deadline
} ktimer_req_t;

// RNG request
typedef struct {
    kwork_t work;
    uint8_t* buffer;
    size_t length;
    size_t completed;       // Bytes actually read
} krng_req_t;
```

### CONTAINER_OF Pattern

Recover the containing structure from a work pointer:

```c
#define CONTAINER_OF(ptr, type, member) \
    ((type*)((uint8_t*)(ptr) - offsetof(type, member)))

// Usage
void timer_callback(kwork_t* work) {
    ktimer_req_t* req = CONTAINER_OF(work, ktimer_req_t, work);
    // ... use req->deadline_ms ...
}
```

## Cancellation

Cancellation is **best-effort**:

1. User calls `kcancel(k, work)`
2. Work moves to cancel queue (CANCEL_REQUESTED state)
3. `ktick()` calls `platform_cancel_work()`
4. Platform attempts cancellation:
   - **Success**: Calls `kplatform_cancel_work()`, moves to ready with KERR_CANCELLED
   - **Failure**: Work completes normally with original result

No result code is passed to `kplatform_cancel_work()` because it's always `KERR_CANCELLED`.

## Bulk Submission

Work and cancellations are submitted in bulk to allow batching:

### Example: VirtIO Descriptor Batching

```c
void platform_submit(platform_t* platform, kwork_t* submissions, kwork_t* cancellations) {
    virtio_rng_t* rng = platform->virtio_rng;

    // Process cancellations (best-effort)
    kwork_t* work = cancellations;
    while (work != NULL) {
        if (work->op == KWORK_OP_RNG_READ) {
            // Attempt to cancel (usually too late for virtio-rng)
        }
        work = work->next;
    }

    // Setup all submission descriptors
    work = submissions;
    while (work != NULL) {
        if (work->op == KWORK_OP_RNG_READ) {
            setup_virtio_descriptor(rng, work);
        }
        work = work->next;
    }

    // Single doorbell ring for all descriptors
    if (submissions != NULL) {
        virtqueue_kick(&rng->vq);
    }
}
```

This reduces MMIO/PIO writes and improves performance.

## Backpressure

When a device queue is full:

1. Platform immediately completes work with `KERR_BUSY`
2. Work moves to ready queue
3. Callback receives `work->result == KERR_BUSY`
4. Caller can choose to retry or handle the error

### Example

```c
void on_rng_complete(kwork_t* work) {
    if (work->result == KERR_BUSY) {
        printk("RNG queue full, retrying...\n");
        ksubmit(kernel, work);  // Retry
        return;
    }

    // Handle success
    krng_req_t* req = CONTAINER_OF(work, krng_req_t, work);
    // ... use req->buffer ...
}
```

## Future Work

- **Standing work items**: Persistent work (periodic timers, standing receives)
- **Timer wheel/heap**: O(1) amortized or O(log n) timer operations
- **Multi-core support**: Per-CPU work queues
- **Priority**: Priority-based work scheduling
- **Async I/O**: Block device and network integration

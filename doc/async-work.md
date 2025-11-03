# VMOS Async Work Queue System

## Overview

VMOS uses an **asynchronous work queue system** instead of blocking I/O. All I/O operations (device reads, timers, network packets) are submitted as work items that complete asynchronously via callbacks. This design enables single-threaded concurrency without blocking the event loop.

**Key Characteristics:**
- Zero dynamic allocation (all structures pre-allocated by user)
- Callback-based completion notification
- State machine lifecycle for work items
- Bulk submission to platform layer
- Timer heap for efficient timer management
- Support for standing (persistent) work items

## Core Concepts

### Work Items

All async operations are represented as **work items** (`kwork_t`):

```c
struct kwork {
    uint32_t op;               // Operation type (KWORK_OP_*)
    kwork_callback_t callback; // Completion callback
    void *ctx;                 // User context pointer
    kerr_t result;             // Result code (KERR_OK, KERR_TIMEOUT, etc.)
    uint8_t state;             // Current state (kwork_state_t)
    uint8_t flags;             // KWORK_FLAG_* bits
    kwork_t *next;             // Intrusive list pointers
    kwork_t *prev;
};
```

**Location:** `kernel/kapi.h:54-63`

### Work States

Work items transition through a well-defined state machine:

```c
typedef enum {
    KWORK_STATE_DEAD = 0,             // Not active
    KWORK_STATE_SUBMIT_REQUESTED = 1, // Queued for submission
    KWORK_STATE_LIVE = 2,             // Active (submitted to platform/timer)
    KWORK_STATE_READY = 3,            // Completed, ready for callback
    KWORK_STATE_CANCEL_REQUESTED = 4, // Cancellation requested
} kwork_state_t;
```

**Location:** `kernel/kapi.h:28-34`

### State Transitions

```
Initial state (kwork_init)
    ↓
DEAD (0)
    │
    │ User calls ksubmit()
    ▼
SUBMIT_REQUESTED (1)
    │
    │ kmain_tick() processes submit queue
    ▼
LIVE (2) ──────────────────┐
    │                       │ User calls kcancel()
    │                       ▼
    │                  CANCEL_REQUESTED (4)
    │                       │
    │ Completion            │ Platform cancels
    │ kplatform_complete_   │ kplatform_cancel_work()
    │ work()                │
    ▼                       ▼
READY (3)
    │
    │ kmain_tick() invokes callback
    ▼
DEAD (0)  [or back to LIVE if KWORK_FLAG_STANDING]
```

**Implementation:** `kernel/kernel.c:238-302`

## Operation Types

Work items specify their operation type via the `op` field:

```c
typedef enum {
    KWORK_OP_TIMER = 1,       // Timer expiration
    KWORK_OP_RNG_READ = 2,    // Random number generation
    KWORK_OP_BLOCK_READ = 3,  // Block device read
    KWORK_OP_BLOCK_WRITE = 4, // Block device write
    KWORK_OP_BLOCK_FLUSH = 5, // Block device flush
    KWORK_OP_NET_RECV = 6,    // Network packet receive
    KWORK_OP_NET_SEND = 7,    // Network packet send
} kwork_op_t;
```

**Location:** `kernel/kapi.h:40-48`

Each operation type has a corresponding request structure that embeds `kwork_t`:

- **Timer**: `ktimer_req_t` (deadline, heap pointers)
- **RNG**: `krng_req_t` (buffer, length)
- **Block**: `kblk_req_t` (segments, scatter-gather I/O)
- **Network Receive**: `knet_recv_req_t` (buffer ring)
- **Network Send**: `knet_send_req_t` (packet array)

## Work Item Lifecycle

### 1. Initialization

User allocates and initializes a work item:

```c
// Example: Timer request
ktimer_req_t timer;
kwork_init(&timer.work, KWORK_OP_TIMER, user_ctx, timer_callback, 0);
timer.deadline_ns = current_time + 1000000000ULL; // 1 second from now
```

**Function signature:**
```c
void kwork_init(kwork_t *work, uint32_t op, void *ctx,
                kwork_callback_t callback, uint8_t flags);
```

**Location:** `kernel/kernel.c:209-220`

**Parameters:**
- `work`: Pointer to work item
- `op`: Operation type (`KWORK_OP_*`)
- `ctx`: User context (passed to callback, can be NULL)
- `callback`: Completion callback function
- `flags`: `KWORK_FLAG_STANDING` for persistent work, 0 for one-shot

### 2. Submission

User submits work to the kernel:

```c
kerr_t err = ksubmit(kernel, &timer.work);
if (err != KERR_OK) {
    // Handle error (KERR_BUSY if already submitted, KERR_INVALID if NULL)
}
```

**Function signature:**
```c
kerr_t ksubmit(kernel_t *k, kwork_t *work);
```

**Location:** `kernel/kernel.c:144-170`

**Behavior:**
1. Validates work is in `DEAD` state (returns `KERR_BUSY` otherwise)
2. Transitions to `SUBMIT_REQUESTED`
3. **Timers**: Immediately insert into timer heap, transition to `LIVE`
4. **Other ops**: Add to submit queue for bulk submission

**Important:** `ksubmit()` returns immediately. It does NOT block waiting for completion.

### 3. Bulk Submission to Platform

During `kmain_tick()`, the kernel submits all queued work to the platform in a single batch:

```c
// kernel/kernel.c:297-300
if (k->submit_queue_head != NULL || platform_cancel_head != NULL) {
    platform_submit(&k->platform, k->submit_queue_head, platform_cancel_head);
    k->submit_queue_head = k->submit_queue_tail = NULL;
}
```

**Why bulk submission?**
- Reduces function call overhead
- Allows platform to optimize batch operations
- Simplifies platform interface

### 4. Platform Processing

Platform layer processes submitted work:

**For Timers:**
- Already in kernel timer heap (managed in kernel, not platform)
- Expire during `expire_timers()` in `kmain_tick()`

**For Device I/O (RNG, Block, Network):**
- Platform submits to VirtIO driver
- Driver configures virtqueue and notifies device
- Device processes asynchronously
- Interrupt arrives when complete

**For PCI/MMIO Discovery:**
- Platform synchronously processes request
- Immediately calls `kplatform_complete_work()`

### 5. Completion

Platform marks work as complete:

```c
// Called from platform layer (e.g., in interrupt handler or platform_tick)
kplatform_complete_work(kernel, work, KERR_OK);
```

**Function signature:**
```c
void kplatform_complete_work(kernel_t *k, kwork_t *work, kerr_t result);
```

**Location:** `kernel/kernel.c:305-314`

**Behavior:**
1. Sets `work->result` to provided error code
2. Transitions from `LIVE` to `READY`
3. Adds to ready queue

### 6. Callback Invocation

During `kmain_tick()`, kernel processes ready queue and invokes callbacks:

```c
// kernel/kernel.c:248-269
kwork_t *work = k->ready_queue_head;
while (work != NULL) {
    kwork_t *next = work->next;
    k->ready_queue_head = next;

    // Standing work stays LIVE, one-shot goes DEAD
    if ((work->flags & KWORK_FLAG_STANDING) && work->result == KERR_OK) {
        work->state = KWORK_STATE_LIVE;
    } else {
        work->state = KWORK_STATE_DEAD;
    }

    // Invoke callback
    work->callback(work);

    work = next;
}
```

**Callback Contract:**
- Callback receives pointer to `kwork_t`
- Use `CONTAINER_OF` to recover full request structure
- Check `work->result` for error code
- Access user context via `work->ctx`
- May call `ksubmit()` to resubmit work (common pattern)

### 7. User Callback

User callback processes the result:

```c
void timer_callback(kwork_t *work) {
    ktimer_req_t *timer = CONTAINER_OF(work, ktimer_req_t, work);
    user_t *user = (user_t *)work->ctx;

    if (work->result != KERR_OK) {
        KLOG("Timer failed: %u", work->result);
        return;
    }

    KLOG("Timer expired!");

    // Optionally resubmit for periodic timer
    timer->deadline_ns = kget_time_ns__logonly__() + 1000000000ULL;
    ksubmit(user->kernel, &timer->work);
}
```

**Important:** After callback completes, work is in `DEAD` state (unless `KWORK_FLAG_STANDING`). User must call `ksubmit()` again to reuse it.

## Standing Work

Some operations should **persist** after completion instead of transitioning to `DEAD`. This is called **standing work**.

**Use cases:**
- Network receive buffers (always ready to receive next packet)
- Event notifications (always listening)

**How to enable:**
```c
kwork_init(&recv_req.work, KWORK_OP_NET_RECV, ctx, recv_callback,
           KWORK_FLAG_STANDING);
```

**Behavior:**
- After callback, work returns to `LIVE` state instead of `DEAD`
- Remains active without resubmission
- **Exception:** If `work->result != KERR_OK`, transitions to `DEAD` (error occurred)

**Implementation:** `kernel/kernel.c:257-263`

## Cancellation

Best-effort cancellation is supported via `kcancel()`:

```c
kerr_t kcancel(kernel_t *k, kwork_t *work);
```

**Location:** `kernel/kernel.c:173-190`

**Behavior:**
1. Only works on `LIVE` work (returns `KERR_INVALID` for `DEAD` or `READY`)
2. Transitions to `CANCEL_REQUESTED`
3. Adds to cancel queue
4. Platform attempts cancellation (best-effort)
5. Callback invoked with `result = KERR_CANCELLED`

**Guarantees:**
- **Timers**: Cancellation always succeeds (kernel/kernel.c:277-286)
- **Device I/O**: Best-effort (may complete before cancellation)
- **Network**: May receive packet before cancellation takes effect

**Why best-effort?**
- Device may have already processed request
- Race between cancellation and completion
- Some devices don't support cancellation

## Timer System

Timers are managed by a **min-heap** for efficient operations.

### Timer Structure

```c
typedef struct ktimer_req ktimer_req_t;
struct ktimer_req {
    kwork_t work;        // Embedded work item
    ktime_t deadline_ns; // Absolute deadline in nanoseconds
    ktimer_req_t *parent; // Intrusive heap parent pointer
    ktimer_req_t *left;   // Intrusive heap left child
    ktimer_req_t *right;  // Intrusive heap right child
};
```

**Location:** `kernel/kapi.h:66-73`

### Timer Operations

**Insert:** O(log n)
```c
timer_heap_insert(kernel, timer);
```

**Peek minimum:** O(1)
```c
ktimer_req_t *next_timer = timer_heap_peek_min(kernel);
```

**Extract minimum:** O(log n)
```c
ktimer_req_t *expired = timer_heap_extract_min(kernel);
```

**Delete arbitrary timer:** O(log n)
```c
timer_heap_delete(kernel, timer);
```

**Location:** `kernel/timer_heap.h`, `kernel/timer_heap.c`

### Timer Expiration

Timers are expired during `kmain_tick()`:

```c
// kernel/kernel.c:113-132
static void expire_timers(kernel_t *k) {
    while (1) {
        ktimer_req_t *timer = timer_heap_peek_min(k);

        if (timer == NULL || timer->deadline_ns > k->current_time_ns) {
            break; // No more expired timers
        }

        // Extract from heap
        timer_heap_extract_min(k);

        // Move to ready queue
        timer->work.result = KERR_OK;
        timer->work.state = KWORK_STATE_READY;
        enqueue_ready(k, &timer->work);
    }
}
```

**Efficient:** Only processes expired timers, stops as soon as an unexpired timer is found.

### Timer Example

```c
// One-shot timer
ktimer_req_t timer;
kwork_init(&timer.work, KWORK_OP_TIMER, user, on_timeout, 0);
timer.deadline_ns = current_time + 1000000000ULL; // 1 second
ksubmit(kernel, &timer.work);

void on_timeout(kwork_t *work) {
    ktimer_req_t *timer = CONTAINER_OF(work, ktimer_req_t, work);
    KLOG("Timer expired!");
}
```

**Periodic timer:**
```c
void on_periodic(kwork_t *work) {
    ktimer_req_t *timer = CONTAINER_OF(work, ktimer_req_t, work);
    KLOG("Tick!");

    // Resubmit for next tick
    timer->deadline_ns += 1000000000ULL; // +1 second
    ksubmit(get_kernel(), &timer->work);
}
```

## Work Queues

The kernel maintains three intrusive linked lists:

### 1. Submit Queue (Doubly-Linked)

**Purpose:** Holds work items waiting to be submitted to platform

**Structure:**
```c
kernel_t {
    kwork_t *submit_queue_head;
    kwork_t *submit_queue_tail;
};
```

**Operations:**
- Enqueue: `enqueue_submit()` - O(1) append
- Process: Entire queue submitted in bulk via `platform_submit()`
- Cleared after submission

**Location:** `kernel/kernel.c:83-93`

### 2. Cancel Queue (Singly-Linked)

**Purpose:** Holds work items with cancellation requested

**Structure:**
```c
kernel_t {
    kwork_t *cancel_queue_head;
};
```

**Operations:**
- Enqueue: `enqueue_cancel()` - O(1) prepend
- Process: Timer cancellations handled in kernel, others forwarded to platform
- Cleared after processing

**Location:** `kernel/kernel.c:95-99`, processed at `kernel/kernel.c:272-294`

### 3. Ready Queue (Singly-Linked)

**Purpose:** Holds completed work items ready for callback

**Structure:**
```c
kernel_t {
    kwork_t *ready_queue_head;
};
```

**Operations:**
- Enqueue: `enqueue_ready()` - O(1) prepend
- Process: Iterate through queue, invoke callbacks, clear queue
- Cleared after callbacks run

**Location:** `kernel/kernel.c:101-105`, processed at `kernel/kernel.c:248-269`

## Error Codes

Work callbacks receive a result code:

```c
typedef uint32_t kerr_t;

#define KERR_OK 0        // Success
#define KERR_BUSY 1      // Resource busy (ksubmit on active work)
#define KERR_INVALID 2   // Invalid argument
#define KERR_CANCELLED 3 // Operation cancelled
#define KERR_TIMEOUT 4   // Timeout
#define KERR_NO_DEVICE 5 // Device not available
#define KERR_IO_ERROR 6  // I/O error (bad sector, transmission failure)
#define KERR_NO_SPACE 7  // Device full (block) or queue full (network)
```

**Location:** `kernel/kapi.h:16-26`

**Usage in callbacks:**
```c
void my_callback(kwork_t *work) {
    if (work->result != KERR_OK) {
        switch (work->result) {
            case KERR_CANCELLED:
                KLOG("Operation cancelled");
                break;
            case KERR_TIMEOUT:
                KLOG("Timeout");
                break;
            case KERR_IO_ERROR:
                KLOG("Device I/O error");
                break;
            default:
                KLOG("Unknown error: %u", work->result);
        }
        return;
    }

    // Process successful result...
}
```

## Request Types

### Timer Request

```c
ktimer_req_t timer;
kwork_init(&timer.work, KWORK_OP_TIMER, ctx, callback, 0);
timer.deadline_ns = current_time + delay_ns;
ksubmit(kernel, &timer.work);
```

**Fields:** `deadline_ns` (absolute time in nanoseconds)

### RNG Request

```c
krng_req_t rng_req;
uint8_t random_bytes[32];
kwork_init(&rng_req.work, KWORK_OP_RNG_READ, ctx, callback, 0);
rng_req.buffer = random_bytes;
rng_req.length = 32;
ksubmit(kernel, &rng_req.work);
```

**Fields:**
- `buffer`: Output buffer for random data
- `length`: Bytes requested
- `completed`: Bytes actually filled (set on completion)

**Location:** `kernel/kapi.h:76-82`

### Block Request

```c
kblk_req_t blk_req;
kblk_segment_t segment;
segment.sector = 0;
segment.buffer = aligned_buffer; // Must be 4K-aligned
segment.num_sectors = 8;

kwork_init(&blk_req.work, KWORK_OP_BLOCK_READ, ctx, callback, 0);
blk_req.segments = &segment;
blk_req.num_segments = 1;
ksubmit(kernel, &blk_req.work);
```

**Fields:**
- `segments`: Array of I/O segments (scatter-gather)
- `num_segments`: Number of segments
- `completed_sectors`: Sectors actually transferred

**Location:** `kernel/kapi.h:85-100`

### Network Receive Request (Standing)

```c
knet_recv_req_t recv_req;
knet_buffer_t buffers[4];
for (int i = 0; i < 4; i++) {
    buffers[i].buffer = malloc(2048);
    buffers[i].buffer_size = 2048;
}

kwork_init(&recv_req.work, KWORK_OP_NET_RECV, ctx, callback,
           KWORK_FLAG_STANDING);
recv_req.buffers = buffers;
recv_req.num_buffers = 4;
ksubmit(kernel, &recv_req.work);
```

**Fields:**
- `buffers`: Ring buffer array
- `num_buffers`: Number of buffers
- `buffer_index`: Which buffer was filled (set on completion)

**Usage:** Typically used as standing work. When packet arrives, callback is invoked with `buffer_index` indicating which buffer contains the packet.

**Location:** `kernel/kapi.h:112-118`

### Network Send Request

```c
knet_send_req_t send_req;
knet_buffer_t packet;
packet.buffer = packet_data;
packet.buffer_size = packet_len;

kwork_init(&send_req.work, KWORK_OP_NET_SEND, ctx, callback, 0);
send_req.packets = &packet;
send_req.num_packets = 1;
ksubmit(kernel, &send_req.work);
```

**Fields:**
- `packets`: Array of packets to send
- `num_packets`: Number of packets
- `packets_sent`: Number actually sent (set on completion)

**Location:** `kernel/kapi.h:121-127`

## Platform-Specific State

Each request type may contain platform-specific data:

```c
typedef struct {
    kwork_t work;
    uint8_t *buffer;
    size_t length;
    size_t completed;
    krng_req_platform_t platform; // Platform-specific fields (64 bytes)
} krng_req_t;
```

**Platform types are defined in `platform/*/platform_impl.h`:**

```c
// Example from platform/arm64/platform_impl.h
typedef struct {
    uint32_t desc_index;        // Virtqueue descriptor index
    uint32_t _padding[15];      // Total 64 bytes
} krng_req_platform_t;
```

**Why?**
- Avoids dynamic allocation
- Platform stores device-specific state (descriptor indices, DMA addresses, etc.)
- Opaque to user code

## Best Practices

### 1. Always Check Result

```c
void callback(kwork_t *work) {
    if (work->result != KERR_OK) {
        // Handle error
        return;
    }
    // Process result
}
```

### 2. Use CONTAINER_OF to Recover Full Structure

```c
void timer_callback(kwork_t *work) {
    ktimer_req_t *timer = CONTAINER_OF(work, ktimer_req_t, work);
    // Now you can access timer->deadline_ns, etc.
}
```

### 3. Don't Resubmit Active Work

```c
// WRONG
ksubmit(kernel, &work); // Returns KERR_BUSY if already active

// RIGHT - wait for callback to complete first
void callback(kwork_t *work) {
    // Work is now DEAD, safe to resubmit
    ksubmit(get_kernel(), work);
}
```

### 4. Use Standing Work for Persistent Operations

```c
// Network receive should stay active
kwork_init(&recv_req.work, KWORK_OP_NET_RECV, ctx, callback,
           KWORK_FLAG_STANDING);
```

### 5. Absolute Deadlines for Timers

```c
// Get current time
ktime_t now = kget_time_ns__logonly__();

// Set absolute deadline
timer.deadline_ns = now + 1000000000ULL; // 1 second from now
```

### 6. Allocate Work Items Statically

```c
// GOOD - static allocation
static ktimer_req_t periodic_timer;

// AVOID - stack allocation that goes out of scope
void setup() {
    ktimer_req_t timer; // Dangerous! Goes out of scope
    ksubmit(kernel, &timer.work); // Callback will use freed memory
}
```

### 7. Don't Block in Callbacks

```c
// WRONG
void callback(kwork_t *work) {
    while (condition) { } // Blocks event loop
}

// RIGHT
void callback(kwork_t *work) {
    // Process quickly, submit new work if needed
    if (more_work_needed) {
        ksubmit(kernel, &next_work);
    }
}
```

## Debugging

### Work History (Debug Builds Only)

In debug builds (`KDEBUG` defined), kernel records last 16 work state transitions:

```c
void kdebug_dump_work_history(void);
```

**Output:**
```
Last work transitions:
  0x12345678: DEAD -> SUBMIT_REQUESTED @ 1000000ns
  0x12345678: SUBMIT_REQUESTED -> LIVE @ 1000010ns
  0x12345678: LIVE -> READY @ 1500000ns
  0x12345678: READY -> DEAD @ 1500020ns
```

**Location:** `kernel/kernel.c:8-79`

### Common Issues

**Work stuck in LIVE state:**
- Platform didn't call `kplatform_complete_work()`
- Device interrupt not firing
- IRQ not enabled for device

**KERR_BUSY on ksubmit:**
- Work still active from previous submission
- Must wait for callback before resubmitting

**Callback not invoked:**
- Work never completed (check device)
- Event loop not running (`kmain_tick` not called)

**Timer not expiring:**
- Deadline in the past (use absolute time, not relative)
- Timer heap corrupted (use debug build with assertions)

## Performance Characteristics

| Operation | Time Complexity |
|-----------|----------------|
| `ksubmit()` (non-timer) | O(1) |
| `ksubmit()` (timer) | O(log n) timers |
| `kcancel()` (non-timer) | O(1) |
| `kcancel()` (timer) | O(log n) timers |
| Expire timers | O(k log n) where k = expired |
| Process ready queue | O(m) where m = ready |
| `platform_submit()` | O(n) submissions |

**Memory usage:**
- Submit queue: O(n) pending submissions
- Cancel queue: O(n) pending cancellations
- Ready queue: O(m) completed work
- Timer heap: O(t) active timers

**Zero dynamic allocation:** All structures are pre-allocated by user code.

## Summary

The async work queue system provides:

✅ **Non-blocking I/O** - All operations complete asynchronously
✅ **Zero allocation** - User pre-allocates all structures
✅ **State machine** - Clear lifecycle (DEAD → SUBMIT → LIVE → READY → DEAD)
✅ **Bulk submission** - Efficient batch operations
✅ **Timer heap** - O(log n) timer operations
✅ **Standing work** - Persistent operations without resubmission
✅ **Best-effort cancellation** - Cancel active work items
✅ **Error handling** - Comprehensive error codes

For more details:
- **Architecture**: See [architecture.md](architecture.md)
- **Platform contract**: See [platform-api.md](platform-api.md)
- **VirtIO drivers**: See [virtio.md](virtio.md)

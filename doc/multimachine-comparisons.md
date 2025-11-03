# The Machine Pattern: Comparisons to Other Concurrency Models

## Introduction

This document compares the machine pattern (described in
[multimachine.md](multimachine.md)) to other concurrency and communication
paradigms. The key insight is that the machine pattern is a **generalized
select/channel mechanism** expressed through callback-based state machines with
zero-copy message passing.

## Table of Contents

1. [Actor Systems](#actor-systems)
2. [CSP (Communicating Sequential Processes)](#csp-communicating-sequential-processes)
3. [Select/Choice Mechanisms](#selectchoice-mechanisms)
4. [Sync vs Async Communication](#sync-vs-async-communication)
5. [Queues and Channels](#queues-and-channels)
6. [Other Paradigms](#other-paradigms)
7. [Summary Matrix](#summary-matrix)
8. [Unique Characteristics](#unique-characteristics)

---

## Actor Systems

Actor systems (Erlang, Akka, Orleans) use asynchronous message passing between independent actors.

### Similarities

- Asynchronous message passing
- State machines processing requests
- Decoupled components
- Location transparency (actors can be remote)

### Key Differences

| Aspect | Actor Systems | Machine Pattern |
|--------|--------------|----------------|
| **Message queues** | Each actor has a mailbox queue | No per-machine queues; direct delegation |
| **Message lifetime** | Messages copied into mailbox | Zero-copy via async stack (embedded requests) |
| **Reply mechanism** | Send reply message to sender | Explicit completion chain via `requester` field |
| **Actor discovery** | Dynamic addressing (PIDs, names) | Static wiring (machine references set at init) |
| **Synchronous operations** | Block actor or use ask/await pattern | Separate `query()` method with immediate return |
| **Fault isolation** | Supervisor trees, crash recovery | Not addressed (kernel-level primitive) |
| **Distribution** | Built-in remote actors | Local only (single address space) |

### Example Comparison

**Actor System (Erlang):**
```erlang
% Send message
ActorPid ! {fetch_data, Params},

% Receive reply
receive
    {data_result, Result} -> handle(Result)
after 5000 ->
    timeout
end.
```

**Machine Pattern:**
```c
// Submit work
fetch_req_t req;
req.work.requester = &my_machine;
req.work.callback = fetch_callback;
data_machine.submit(&data_machine, &req.work);

// Completion
void fetch_callback(kwork_t* work, kerr_t err) {
    fetch_req_t* req = CONTAINER_OF(work, fetch_req_t, work);
    handle(req->result);
}
```

### Assessment

The machine pattern is like **zero-copy actors with pre-allocated message buffers** and **explicit return paths**. It trades dynamic flexibility for performance and determinism.

---

## CSP (Communicating Sequential Processes)

CSP (Go, Occam, Rust channels) uses channels for synchronization and communication.

### CSP Characteristics

- Channels as first-class communication primitives
- Rendezvous (unbuffered) channels: synchronous
- Buffered channels: asynchronous
- `select` for choosing among multiple channel operations
- Channels can be passed as values

### Machine Pattern Equivalent

The machine pattern **does not** have explicit channels, but **can implement all CSP patterns**:

| CSP Concept | Machine Pattern Equivalent |
|-------------|---------------------------|
| **Unbuffered channel send** | Submit work, wait for completion (rendezvous) |
| **Buffered channel send** | Submit work, immediate completion if buffer available |
| **Channel receive** | Machine waits for work submission |
| **Select on multiple channels** | Submit to multiple machines, handle first completion |
| **Channel closing** | Machine stops accepting work (error on submit) |

### Rendezvous Example

**CSP (Go):**
```go
ch := make(chan int)  // Unbuffered = rendezvous

// Sender blocks until receiver ready
ch <- 42

// Receiver blocks until sender ready
v := <-ch
```

**Machine Pattern:**
```c
typedef struct {
    kwork_t* waiting_sender;
    kwork_t* waiting_receiver;
    void* rendezvous_data;
} rendezvous_machine_t;

void rendezvous_submit(void* state, kwork_t* work) {
    rendezvous_machine_t* ch = (rendezvous_machine_t*)state;
    channel_req_t* req = CONTAINER_OF(work, channel_req_t, work);

    if (req->op == CHANNEL_OP_SEND) {
        if (ch->waiting_receiver != NULL) {
            // Receiver waiting - rendezvous occurs!
            kwork_t* receiver = ch->waiting_receiver;
            ch->waiting_receiver = NULL;

            // Transfer data
            transfer_data(receiver, req->data);

            // Complete both sender and receiver
            receiver->requester->complete(receiver->requester->state,
                                         receiver, KERR_OK);
            work->requester->complete(work->requester->state,
                                     work, KERR_OK);
        } else {
            // No receiver yet - sender waits
            ch->waiting_sender = work;
            ch->rendezvous_data = req->data;
            work->state = KWORK_STATE_LIVE;  // Blocking
        }
    }
    else if (req->op == CHANNEL_OP_RECV) {
        if (ch->waiting_sender != NULL) {
            // Sender waiting - rendezvous occurs!
            kwork_t* sender = ch->waiting_sender;
            ch->waiting_sender = NULL;

            // Transfer data
            transfer_data(work, ch->rendezvous_data);

            // Complete both
            sender->requester->complete(sender->requester->state,
                                       sender, KERR_OK);
            work->requester->complete(work->requester->state,
                                     work, KERR_OK);
        } else {
            // No sender yet - receiver waits
            ch->waiting_receiver = work;
            work->state = KWORK_STATE_LIVE;  // Blocking
        }
    }
}
```

The "blocking" is implicit - work stays LIVE until the rendezvous completes.

### Buffered Channel Example

**CSP (Go):**
```go
ch := make(chan int, 10)  // Buffer capacity = 10

ch <- 42  // Non-blocking if buffer not full
v := <-ch  // Non-blocking if buffer not empty
```

**Machine Pattern:**
```c
typedef struct {
    machine_t machine;

    void* buffer[BUFFER_CAPACITY];
    size_t buffer_head;
    size_t buffer_tail;
    size_t buffer_count;

    kwork_t* waiting_senders[MAX_WAITING];
    size_t waiting_sender_count;
} buffered_channel_t;

void buffered_channel_submit(void* state, kwork_t* work) {
    buffered_channel_t* ch = (buffered_channel_t*)state;
    channel_req_t* req = CONTAINER_OF(work, channel_req_t, work);

    if (req->op == CHANNEL_OP_SEND) {
        if (ch->buffer_count < BUFFER_CAPACITY) {
            // Buffer has space - enqueue and complete immediately
            ch->buffer[ch->buffer_tail] = req->data;
            ch->buffer_tail = (ch->buffer_tail + 1) % BUFFER_CAPACITY;
            ch->buffer_count++;

            work->requester->complete(work->requester->state,
                                     work, KERR_OK);
        } else {
            // Buffer full - sender blocks
            ch->waiting_senders[ch->waiting_sender_count++] = work;
            work->state = KWORK_STATE_LIVE;
        }
    }
    else if (req->op == CHANNEL_OP_RECV) {
        if (ch->buffer_count > 0) {
            // Data available - dequeue and complete immediately
            req->received_data = ch->buffer[ch->buffer_head];
            ch->buffer_head = (ch->buffer_head + 1) % BUFFER_CAPACITY;
            ch->buffer_count--;

            work->requester->complete(work->requester->state,
                                     work, KERR_OK);

            // Unblock waiting sender if any
            if (ch->waiting_sender_count > 0) {
                kwork_t* sender = ch->waiting_senders[0];
                // Shift queue...
                sender->requester->complete(sender->requester->state,
                                           sender, KERR_OK);
                ch->waiting_sender_count--;
            }
        } else {
            // No data - receiver blocks (store work pointer)
            // ... implementation
        }
    }
}
```

### Assessment

The machine pattern **is CSP-complete**: it can implement all CSP primitives. However:

- **CSP**: Language-level constructs (channels, select)
- **Machine pattern**: Library-level patterns (implement as machines)

CSP has better ergonomics (syntax), machine pattern has better performance (zero-copy) and composability (machines stack).

---

## Select/Choice Mechanisms

**Key Insight**: The machine pattern already supports generalized select/choice through **multiple concurrent submissions** with **completion-based control flow**.

### CSP Select

**Go select:**
```go
select {
case v := <-ch1:
    handle1(v)
case v := <-ch2:
    handle2(v)
case <-time.After(timeout):
    handleTimeout()
}
```

### Machine Pattern Equivalent

A machine can issue multiple concurrent submissions and handle completions as they arrive:

```c
typedef struct {
    kwork_t work;

    // Multiple concurrent child requests (async stack pattern)
    network_req_t network_req;
    storage_req_t storage_req;
    timer_req_t timer_req;

    // Select state
    uint32_t strategy;         // FIRST_WINS, ALL, ANY_N_OF_M
    uint32_t completed_count;
    uint32_t required_count;
    bool completed;
} select_req_t;

void select_machine_submit(void* state, kwork_t* work) {
    select_machine_t* m = (select_machine_t*)state;
    select_req_t* req = CONTAINER_OF(work, select_req_t, work);

    req->completed_count = 0;
    req->completed = false;

    // Submit ALL branches concurrently
    req->network_req.work.requester = &select_machine;
    req->network_req.work.callback = select_branch_callback;
    m->network->submit(m->network->state, &req->network_req.work);

    req->storage_req.work.requester = &select_machine;
    req->storage_req.work.callback = select_branch_callback;
    m->storage->submit(m->storage->state, &req->storage_req.work);

    req->timer_req.work.requester = &select_machine;
    req->timer_req.work.callback = select_branch_callback;
    m->timer->submit(m->timer->state, &req->timer_req.work);

    work->state = KWORK_STATE_LIVE;
}

void select_machine_complete(void* state, kwork_t* work, kerr_t err) {
    select_machine_t* m = (select_machine_t*)state;

    // Which branch completed?
    select_req_t* req;
    if (work == &req->network_req.work) {
        req = CONTAINER_OF(&req->network_req, select_req_t, network_req);
        handle_network_completion(req, err);
    } else if (work == &req->storage_req.work) {
        req = CONTAINER_OF(&req->storage_req, select_req_t, storage_req);
        handle_storage_completion(req, err);
    } else if (work == &req->timer_req.work) {
        req = CONTAINER_OF(&req->timer_req, select_req_t, timer_req);
        handle_timeout(req, err);
    }

    req->completed_count++;

    // Select strategy
    switch (req->strategy) {
        case FIRST_WINS:
            if (!req->completed) {
                req->completed = true;

                // Cancel all other branches
                if (work != &req->network_req.work)
                    m->network->cancel(m->network->state, &req->network_req.work);
                if (work != &req->storage_req.work)
                    m->storage->cancel(m->storage->state, &req->storage_req.work);
                if (work != &req->timer_req.work)
                    m->timer->cancel(m->timer->state, &req->timer_req.work);

                // Forward completion to parent
                req->work.requester->complete(req->work.requester->state,
                                             &req->work, err);
            }
            break;

        case ALL:
            // Wait for all branches
            if (req->completed_count == 3) {
                req->work.requester->complete(req->work.requester->state,
                                             &req->work, KERR_OK);
            }
            break;

        case ANY_N_OF_M:
            // Wait for N completions
            if (req->completed_count >= req->required_count) {
                if (!req->completed) {
                    req->completed = true;
                    // Cancel remaining
                    // ... forward completion
                }
            }
            break;
    }
}
```

### Supported Select Patterns

The machine pattern supports all classic select/choice patterns:

1. **First-to-complete (race)**: Handle first completion, cancel others
2. **Timeout**: Submit timer alongside operation, first wins
3. **Wait-for-all (join/barrier)**: Count completions, forward when all done
4. **Wait-for-N-of-M (quorum)**: Forward when N branches complete
5. **Priority select**: Check completions in priority order
6. **Default case**: Submit with immediate timeout

### Example: Network Request with Timeout

**Go:**
```go
select {
case result := <-networkCh:
    return result
case <-time.After(5 * time.Second):
    return timeout_error
}
```

**Machine Pattern:**
```c
typedef struct {
    kwork_t work;
    network_req_t network_req;
    timer_req_t timer_req;
    bool completed;
} timeout_req_t;

void timeout_machine_submit(void* state, kwork_t* work) {
    timeout_machine_t* m = (timeout_machine_t*)state;
    timeout_req_t* req = CONTAINER_OF(work, timeout_req_t, work);

    req->completed = false;

    // Submit network request
    req->network_req.work.requester = &timeout_machine;
    m->network->submit(m->network->state, &req->network_req.work);

    // Submit timeout
    req->timer_req.work.requester = &timeout_machine;
    req->timer_req.timeout_ns = 5000000000;  // 5 seconds
    m->timer->submit(m->timer->state, &req->timer_req.work);
}

void timeout_machine_complete(void* state, kwork_t* work, kerr_t err) {
    timeout_machine_t* m = (timeout_machine_t*)state;
    timeout_req_t* req;

    if (work == &req->network_req.work) {
        req = CONTAINER_OF(&req->network_req, timeout_req_t, network_req);
        if (!req->completed) {
            req->completed = true;
            // Cancel timer
            m->timer->cancel(m->timer->state, &req->timer_req.work);
            // Forward network result
            req->work.requester->complete(req->work.requester->state,
                                         &req->work, err);
        }
    } else if (work == &req->timer_req.work) {
        req = CONTAINER_OF(&req->timer_req, timeout_req_t, timer_req);
        if (!req->completed) {
            req->completed = true;
            // Cancel network request
            m->network->cancel(m->network->state, &req->network_req.work);
            // Forward timeout error
            req->work.requester->complete(req->work.requester->state,
                                         &req->work, KERR_TIMEOUT);
        }
    }
}
```

### Assessment

**Select/choice is a first-class pattern** in the machine model. The `complete()` method is the control flow primitive where all choice logic lives.

**Advantages over language-level select:**
- More flexible: arbitrary logic in `complete()`
- Composable: select machines stack like any other machine
- Explicit: control flow is visible code, not hidden syntax

**Disadvantages:**
- More verbose: manual completion tracking
- No compiler help: easy to forget to cancel branches

---

## Sync vs Async Communication

### The Key Insight: query() is Just an Optimization

The distinction between `query()` (synchronous) and `submit()` (asynchronous) is **performance**, not semantics.

**Functionally equivalent:**

```c
// Option 1: query() - synchronous fast path
mmio_read_req_t req;
req.work.op = MMIO_QUERY_READ;
req.address = 0x1000;
platform.query(platform.state, &req.work);
uint32_t value = req.value;  // Immediate

// Option 2: submit() - asynchronous, but completes immediately
mmio_read_req_t req;
req.work.requester = &my_machine;
req.work.callback = mmio_read_callback;
req.work.op = MMIO_OP_READ;
req.address = 0x1000;
platform.submit(platform.state, &req.work);
// Platform calls: req.work.requester->complete(...) immediately
// Callback invoked in next tick()

void mmio_read_callback(kwork_t* work, kerr_t err) {
    mmio_read_req_t* req = CONTAINER_OF(work, mmio_read_req_t, work);
    uint32_t value = req->value;  // Available after callback
}
```

**Difference is latency:**
- `query()`: Zero overhead, immediate return
- `submit()`: State transitions, callback queuing, deferred invocation

### When to Use Each

**Use `query()` for:**
- MMIO reads/writes
- PCI configuration space access
- Reading current time
- Quick table lookups
- Anything with negligible latency

**Use `submit()` for:**
- Network I/O
- Disk I/O
- Timers (async wait)
- Any operation that may block
- Any operation with non-trivial latency

### Rendezvous as Async

CSP rendezvous (unbuffered channel) is just async with implicit waiting:

```c
// Send to unbuffered channel = submit work and wait for completion
channel.submit(state, &send_req.work);
// Work stays LIVE until receiver arrives
// Completion happens when rendezvous occurs
```

The "synchronous" behavior emerges from the rendezvous semantics, not from blocking the machine.

---

## Queues and Channels

### What Queues Actually Exist?

Looking at VMOS and the machine pattern:

#### 1. Work Submission Queue (Bulk Submission Optimization)

```
DEAD → SUBMIT_REQUESTED → [bulk submission in tick()] → LIVE
```

- User calls `ksubmit()` → queues for bulk submission
- Runtime processes submission queue in `tick()`
- Optimization to batch platform operations
- Not semantically necessary, just performance

#### 2. Completion Queue (Deferred Callbacks)

```
LIVE → READY → [enqueued] → [callback in tick()] → DEAD
```

- Platform completes work → runtime marks READY
- Work enqueued in ready queue
- `runtime_tick()` drains queue, invokes callbacks
- Ensures callbacks run outside interrupt context

From multimachine.md lines 1279-1299:
```c
void runtime_complete(void* state, kwork_t* work, kerr_t err) {
    runtime_t* rt = (runtime_t*)state;
    // Don't invoke callback immediately
    work->state = KWORK_STATE_READY;
    work->completion_err = err;
    enqueue_ready(rt, work);  // Enqueue for later
}

void runtime_tick(void* state) {
    runtime_t* rt = (runtime_t*)state;
    kwork_t* work;
    while ((work = dequeue_ready(rt)) != NULL) {
        // Invoke callback now
        work->callback(work, work->completion_err);
        work->state = KWORK_STATE_DEAD;
    }
}
```

#### 3. No Inter-Machine Queues

Machines **do not** have per-machine mailboxes. Instead:
- Direct delegation via `machine->submit(state, work)`
- Synchronous call chain for submission
- Asynchronous completion chain via `requester->complete()`

This is different from actor systems where each actor has a mailbox.

#### 4. Device-Level Queues (Hardware)

- VirtIO virtqueues (ring buffers for device communication)
- Only at the leaf level (actual hardware)

### Channel Patterns

Channels can be implemented as machines:

```c
typedef struct {
    machine_t machine;
    void* buffer[CAPACITY];
    size_t head, tail, count;
    kwork_t* waiting_senders[MAX_WAITING];
    kwork_t* waiting_receivers[MAX_WAITING];
} channel_machine_t;
```

Operations:
- `CHANNEL_OP_SEND`: Submit data to channel
- `CHANNEL_OP_RECV`: Receive data from channel

The channel machine manages buffering and blocking semantics.

---

## Other Paradigms

### 1. Continuation-Passing Style (CPS)

The async stack pattern is **CPS with pre-allocated continuations**:

```c
// Traditional CPS (allocates continuation)
submit(work, λ(result) -> {
    handle(result);
})

// Machine pattern (embeds continuation)
work->requester    // Continuation machine
work->callback     // Continuation function
```

### 2. Futures/Promises

Similar to futures:

```c
// JavaScript Promise
fetch(url).then(result => handle(result))

// Machine pattern
fetch_req.work.callback = handle_result;
network.submit(&network, &fetch_req.work);
```

**Differences:**
- **No promise objects**: Work item is both request and future
- **Not chainable**: No `.then().then()` (but can be implemented)
- **Zero allocation**: Async stack pre-allocates entire chain

### 3. Async/Await

The async stack mirrors async/await's state machine:

```rust
// Rust async/await
async fn a() -> Result<T> {
    b().await
}
async fn b() -> Result<T> {
    c().await
}

// Machine pattern (explicit state)
struct A_req {
    kwork_t work;
    B_req b_req;  // Embedded state for b()
}
struct B_req {
    kwork_t work;
    C_req c_req;  // Embedded state for c()
}
```

Both create a "stack" of async state, but machine pattern does it **explicitly** with embedded structs.

### 4. Event Loops / Reactor Pattern (libuv, io_uring)

Very similar architecture:

| Event Loop | Machine Pattern |
|------------|----------------|
| Event loop iteration | `tick()` |
| `epoll_wait()` / `io_uring_wait()` | `wait()` |
| I/O callbacks | Work completion callbacks |
| Event submission | `submit()` |

**Difference**: Machine pattern adds **composable layers** on top of the event loop.

### 5. SEDA (Staged Event-Driven Architecture)

SEDA has processing stages connected by explicit queues.

**SEDA:**
```
Stage 1 → Queue → Stage 2 → Queue → Stage 3
```

**Machine pattern:**
```
Machine 1 → (direct call) → Machine 2 → (direct call) → Machine 3
                           ↑ (completion)
```

Machine pattern uses **direct delegation** instead of explicit inter-stage queues.

### 6. Pipeline Architectures (Unix pipes, Flow-Based Programming)

**Pipelines**: Data flows through, transforming at each stage (unidirectional)
```
input | stage1 | stage2 | stage3 | output
```

**Machine pattern**: Request/response (bidirectional)
```
Request:   User → Runtime → Platform → Device
Response:  User ← Runtime ← Platform ← Device
```

### 7. Capability-Based Systems (seL4, Capsicum)

The `machine_t*` pointer is like a **capability**:

- Unforgeable reference (cannot be created from thin air)
- Grants access to operations
- Can be passed around (delegation)
- Different machines = different capabilities
- Access control via reference passing (principle of least privilege)

### 8. Object Capability Model (E language, Joe-E)

`machine_t*` is an **object capability**:

```c
// Only code with machine reference can use it
machine_t* network = get_network_capability();
network->submit(network->state, work);  // Authorized

// Cannot forge reference
machine_t* fake = (machine_t*)0x12345678;  // Will crash
```

### 9. Dataflow / Reactive Programming (RxJS, React)

Similar asynchronous propagation:

**Reactive:** Data changes flow downstream
```
source.subscribe(observer)
```

**Machine pattern:** Requests flow down, completions flow up
```
machine.submit(work)  // Request down
work.callback(result) // Result up
```

Could implement reactive semantics with machines that auto-resubmit on completion.

### 10. Microkernel IPC (seL4, L4, QNX)

Very close analogy:

| Microkernel IPC | Machine Pattern |
|----------------|----------------|
| Server process | Machine |
| IPC message | Work item |
| Send IPC | `submit()` |
| Reply IPC | `complete()` |
| Message copy | Usually copies | Zero-copy (async stack) |
| Synchronous IPC | Blocking send+receive | Rendezvous pattern |
| Asynchronous IPC | Send without reply | Submit with deferred completion |

**Key difference**: Machine pattern has **zero-copy delegation** via embedded requests, whereas microkernel IPC typically copies messages between address spaces.

### 11. Coroutines (Lua, Python generators, Kotlin)

Coroutines yield control and resume later:

```python
def coroutine():
    result = yield request1  # Suspend
    result2 = yield request2 # Suspend
    return final_result
```

Machine pattern equivalent: each yield point is a submission, each resume is a completion:

```c
void machine_complete(void* state, kwork_t* work, kerr_t err) {
    switch (req->resume_point) {
        case 0:  // First completion
            req->result1 = extract_result(work);
            req->resume_point = 1;
            submit_request2();
            break;
        case 1:  // Second completion
            req->result2 = extract_result(work);
            finalize(req);
            break;
    }
}
```

---

## Summary Matrix

| Paradigm | Message Passing | Queues | Sync/Async | Zero-Copy | Composability | Select/Choice |
|----------|----------------|--------|------------|-----------|---------------|---------------|
| **Actor Model** | ✅ Mailbox | ✅ Per-actor | Async only | ❌ | Medium | Via libraries |
| **CSP (channels)** | ✅ Channels | ✅ Buffered channels | Both | ❌ | High | ✅ Built-in |
| **Machine Pattern** | ✅ Delegation | ⚠️ Runtime boundary only | Both | ✅ | High | ✅ Via patterns |
| **Futures/Promises** | ✅ Implicit | ❌ | Async only | ❌ | High (chaining) | Via `race()` |
| **Event Loop** | ✅ Callbacks | ✅ Event queue | Async only | Varies | Low | Via libraries |
| **Microkernel IPC** | ✅ IPC | ✅ Message queues | Both | ❌ (usually) | Medium | Via endpoints |
| **Async/Await** | ✅ Implicit | ❌ | Async only | ❌ | High | Via `select!` macro |
| **Coroutines** | ✅ Yield/resume | ❌ | Both | ❌ | Medium | Via scheduler |

---

## Unique Characteristics

What makes the machine pattern distinctive:

### 1. Async Stack Pattern (Zero-Copy Delegation)

**Unique aspect**: Embedding child requests in parent requests.

This is like **pre-allocating the entire call stack** for an async operation. No other mainstream paradigm does this explicitly.

```c
typedef struct {
    kwork_t work;           // Parent work
    child1_req_t child1;    // Embedded child 1
    child2_req_t child2;    // Embedded child 2 (inside child1)
} parent_req_t;

parent_req_t req;  // Single allocation, entire delegation chain
```

**Benefits:**
- Zero malloc/free in hot path
- Predictable memory usage
- Cache locality
- Compile-time depth bounds

**Trade-off:** Less dynamic than actor mailboxes or channels.

### 2. Dual Sync/Async Paths

Most systems pick one model. Machine pattern **explicitly supports both**:

- `query()`: Synchronous, immediate return
- `submit()`: Asynchronous, callback-based

This is pragmatic: use the right tool for the right operation.

### 3. Explicit Requester Chain

The `requester` field creates an **explicit return path**:

```c
work->requester->complete(work->requester->state, work, err);
```

This enables:
- Full trace of request delegation
- Instrumentation at each layer
- Clear ownership and lifecycle
- Debugging async flows

Most async systems have implicit completion (callbacks registered at submission).

### 4. Per-Machine Operation Scoping

Operation codes are **local to each machine**, not global:

```c
// Same numeric value, different meanings
enum { USER_OP_FETCH = 0 };
enum { RUNTIME_OP_TIMER = 0 };
enum { PLATFORM_OP_MMIO_READ = 0 };
```

This enables:
- Independent evolution of machines
- Swapping machines with same opcode contract
- No global registry needed

Most message-passing systems use global message types.

### 5. Complete() as Control Flow Primitive

The `complete()` method is where **all control flow decisions** happen:

- Which operation completed?
- What strategy (first-wins, all, quorum)?
- Should I cancel others?
- Should I forward completion upstream?
- Should I issue more work?

This is more flexible than language-level `select` or predefined patterns.

### 6. Swappability via Opcode Contract

Machines with the same opcode definitions can be swapped:

```c
machine_t* get_network_stack(config_t cfg) {
    return cfg.minimal ? &minimal_udp_stack : &full_tcp_ip_stack;
}

platform_init(&platform, get_network_stack(config));
```

Both stacks accept same operations, just different implementations. Like **interfaces/traits but for async operations**.

### 7. Static Wiring, Dynamic Behavior

Machine references are **set at initialization** (static wiring), but behavior is **dynamic** (work flows through at runtime).

This is different from:
- **Actors**: Dynamic wiring (send to any PID)
- **Channels**: Dynamic creation and passing
- **Capabilities**: Can be dynamic or static

Static wiring provides:
- Compile-time structure verification
- Predictable performance
- Clear dependency graph

---

## Conclusion

### The Machine Pattern is a Callback-Based CSP

Key realizations:

1. **Multiple concurrent submissions** = issuing to multiple channels
2. **Handling completions** = select on channel receive
3. **Cancellation** = abandoning unselected branches
4. **Complete() method** = choice/control flow primitive
5. **Query() vs Submit()** = optimization, not fundamental difference
6. **Machines are processes**, **Work items are messages**

### Closest Analogy: Capability-Based Microkernel IPC with Zero-Copy

The machine pattern is most similar to:
- **Microkernel IPC** (L4, seL4): Structured message passing between components
- **Object capabilities**: Unforgeable references grant access
- **Zero-copy communication**: Shared memory instead of message copying
- **Async/await**: Explicit state machines for async operations

### More General Than CSP

The machine pattern is **more general** than traditional CSP because:
- `complete()` can implement **any** control flow strategy
- Not limited to predefined select patterns
- Can implement dataflow/reactive semantics
- Can implement actor semantics
- Can implement coroutine semantics

It's a **meta-pattern** that can express other concurrency models.

### Strength: Composable Zero-Copy Async

The unique strength is **composability** + **zero-allocation** + **flexibility**:

- Stack machines arbitrarily deep (composability)
- Zero malloc/free during delegation (performance)
- Arbitrary control flow in complete() (flexibility)
- Swappable implementations (modularity)
- Both sync and async operations (pragmatism)

This combination is rare in mainstream concurrency systems.

---

*End of document.*

# The Machine Pattern: Composable State Machine Architecture

## Introduction

This document presents a system architecture pattern based on composable state machines with a uniform interface. The **machine pattern** enables building complex systems from simple, swappable components while maintaining zero-allocation performance for asynchronous operations.

### What is the Machine Pattern?

The machine pattern treats every system component as a **state machine** that:
- Accepts work requests from above
- Processes requests directly or delegates to machines below
- Notifies requesters when operations complete

All machines expose the same interface, but implement different operation semantics. This uniformity enables:
- **Composition**: Stack machines arbitrarily deep
- **Swappability**: Replace machines with compatible alternatives
- **Zero-allocation**: Pre-allocate nested requests (async stack pattern)
- **Performance**: Separate sync and async operation paths

### Core Insights

Four key insights make this the "strongest version" of the pattern:

1. **Async Stack Pattern**: Embed child requests in parent requests, eliminating dynamic allocation during delegation. Like a call stack, but for asynchronous operations.

2. **Uniform Machine Interface**: All machines expose the same methods (`submit`, `complete`, `query`, `tick`, `wait`). This enables composition, testing, and swapping.

3. **Sync/Async Separation**: Performance-critical synchronous operations use `query()` with immediate return. Asynchronous operations use `submit()` with later completion via callback.

4. **Operation Code Scoping**: Operation codes are scoped per-machine. The same integer opcode means different things to different machines. This enables machines with matching opcode spaces to be swapped.

### Document Structure

This document has three parts:

**Part 1: The Pattern** - Pure design independent of any specific system. Presents the abstract machine interface, async stack pattern, completion semantics, and swappability.

**Part 2: Relation to VMOS** - Shows how VMOS demonstrates this pattern and how it could evolve to fully embrace it.

**Part 3: Application Guide** - Practical guidance for applying the pattern in your own systems.

---

# Part 1: The Pattern

## 1. The Machine Interface

Every machine exposes a uniform interface:

```c
typedef struct machine machine_t;

struct machine {
    void* state;  // Pointer to concrete state (e.g., net_stack_t*)

    // Async operations
    void (*submit)(void* state, kwork_t* work);
    void (*cancel)(void* state, kwork_t* work);
    void (*complete)(void* state, kwork_t* work, kerr_t err);

    // Sync operations
    void (*query)(void* state, kwork_t* work);

    // Lifecycle
    void (*tick)(void* state);
    ktime_t (*next_delay)(void* state);
    ktime_t (*wait)(void* state, ktime_t timeout_ns);
};
```

### Method Semantics

**submit(state, work)** - Accept asynchronous work request
- Work transitions to LIVE state
- Machine processes or delegates to lower layers
- Returns immediately (non-blocking)
- Eventually calls `work->requester->complete()` when done

**cancel(state, work)** - Request cancellation of in-flight work
- Best-effort: some operations support cancellation, some don't
- Propagates down: machine may cancel child work in lower layers
- Work still completes (with cancellation error or original result)

**complete(state, work, err)** - Notification that work completed
- Called by child machine (or internal completion logic)
- Machine processes result, may update state
- May forward completion upstream to `work->requester->complete()`
- Eventually invokes `work->callback()` to notify original submitter

**query(state, work)** - Synchronous operation with immediate return
- Returns immediately with result in work structure
- Used for performance-critical operations (MMIO reads, lookups)
- No state transitions, no queuing
- Separate opcode space from async operations

**tick(state)** - Process pending events
- Called periodically by parent machine or event loop
- Process completions, timers, deferred work
- Batch processing opportunity

**next_delay(state)** - Time until next event needs processing
- Returns nanoseconds until next scheduled event
- Returns 0 if work ready now
- Returns UINT64_MAX if no pending events

**wait(state, timeout_ns)** - Block until events occur or timeout
- Only implemented by root machine (event loop)
- Blocks on OS primitives (kqueue, epoll, WFI instruction)
- Returns current time when events ready

### The State Field

The `void* state` field points to the machine's concrete state structure. Each machine defines its own state type:

```c
// Network stack machine concrete state
typedef struct {
    machine_t machine;       // Interface (could be elsewhere)

    // Network stack specific state
    arp_cache_t arp_cache;
    ip_addr_t local_ip;
    mac_addr_t local_mac;

    // Delegate machine references
    machine_t* virtio_net;   // Child machine
} net_stack_t;

// Machine methods cast void* to net_stack_t*
void net_stack_submit(void* state, kwork_t* work) {
    net_stack_t* net = (net_stack_t*)state;
    // Use net->arp_cache, net->virtio_net, etc.
}
```

This pattern allows:
- Type-safe access within machine implementations
- Uniform interface for all machines
- Static or dynamic allocation of machine state
- Embedding machines in larger structures

## 2. Work Items and the Requester Pattern

Work items are the messages passed between machines:

```c
typedef uint32_t ktime_t;  // Nanosecond timestamp
typedef uint32_t kerr_t;   // Error code (0 = success)

struct kwork {
    machine_t* requester;                    // Machine to notify on completion
    void (*callback)(kwork_t*, kerr_t);      // Callback requester invokes
    uint32_t op;                             // Operation code (requester's space)
    uint32_t state;                          // DEAD/SUBMIT/LIVE/READY
};
```

### The Requester Field

The `requester` field points to the machine that submitted this work. When work completes, the completing machine calls:

```c
work->requester->complete(work->requester->state, work, result);
```

This explicit completion mechanism provides:
- **Separation**: Completion logic separate from callback invocation
- **Flexibility**: Requester can process results before invoking callback
- **Batching**: Requester can batch completions in tick()
- **State management**: Requester controls work state transitions

### The Callback Field

The callback is invoked by the requester's `complete()` method (or later in `tick()`):

```c
void requester_complete(void* state, kwork_t* work, kerr_t err) {
    // Process result, update state, etc.

    // Invoke user callback
    work->callback(work, err);

    // Transition work to DEAD
    work->state = KWORK_STATE_DEAD;
}
```

The separation of `complete()` and `callback` allows:
- Deferred callback invocation (batch processing)
- State validation before callbacks
- Cleanup and resource management
- Work resubmission without callback

### Operation Code Scoping

The `op` field contains an operation code interpreted by the **requester**, not the current machine:

```c
// Machine A submits work to machine B
work->requester = A;           // A will receive completion
work->op = A_OP_FETCH_DATA;    // Operation code in A's namespace
work->callback = a_fetch_callback;

B->submit(B->state, work);
```

When B completes the work and calls `A->complete()`, machine A interprets `work->op` to understand what operation completed. This scoping enables:
- Multiple machines to use the same opcode values for different operations
- Swapping machines that share an opcode contract
- Clear separation of concerns

### Work State Machine

Work items transition through states:

```
DEAD: Work item is not in use
  ↓ (user calls submit)
SUBMIT_REQUESTED: Queued for bulk submission
  ↓ (machine processes submit queue)
LIVE: Work actively being processed
  ↓ (machine completes work)
READY: Work completed, callback pending
  ↓ (callback invoked)
DEAD: Work item available for reuse
```

The requester manages state transitions through its `complete()` method and callback invocation.

### Traceability

The requester chain forms a trace through the system:

```
User submits work_A with work_A.requester = user_machine
  ↓
Runtime machine receives work_A, creates work_B with work_B.requester = runtime_machine
  ↓
Platform machine receives work_B, creates work_C with work_C.requester = platform_machine
  ↓
work_C.requester -> work_B.requester -> work_A.requester
(platform)          (runtime)           (user)
```

This chain enables:
- Full trace of request delegation
- Instrumentation at each layer (logging, metrics)
- Export to trace tools (e.g., Perfetto format)
- Debugging complex async flows

## 3. The Async Stack Pattern

### The Problem

When machine A delegates to machine B, B needs a work item to submit to lower layers. Three approaches:

1. **Dynamic allocation**: B allocates `kwork_t` when delegating
   - ❌ Overhead: malloc/free in hot path
   - ❌ Fragmentation: many small allocations
   - ❌ Complexity: need allocator in all contexts

2. **Work pools**: Each machine has pre-allocated work pool
   - ⚠️  Bounded resources: pools can be exhausted
   - ⚠️  Bookkeeping: track which work items are in use
   - ✅ No allocation during delegation

3. **Async stack**: Embed child work in parent request
   - ✅ Zero allocation during delegation
   - ✅ Bounded by initial allocation
   - ✅ Clear ownership
   - ✅ Compile-time stack depth

The async stack pattern is the strongest approach for performance and simplicity.

### The Solution

When defining a request type for machine B, embed the child request that B will submit to machine C:

```c
// Request type for machine B operations
typedef struct {
    kwork_t work;              // work.requester = A (machine that submits to B)
                               // work.op = B_OP_SOMETHING

    // B-specific request parameters
    uint32_t b_param1;
    uint8_t* b_buffer;

    // Embedded child request for machine C
    C_req_t c_req;             // c_req.work.requester = B
                               // c_req.work.op = C_OP_SOMETHING
} B_req_t;
```

Machine A allocates `B_req_t` and submits to B:

```c
// Machine A's code
B_req_t req;
memset(&req, 0, sizeof(req));

req.work.requester = &A_machine;
req.work.callback = a_completion_callback;
req.work.op = B_OP_FETCH;
req.work.state = KWORK_STATE_DEAD;

req.b_param1 = 42;
req.b_buffer = buffer;

B_machine.submit(B_machine.state, &req.work);
```

Machine B receives the work, extracts the parent request using `CONTAINER_OF`, then extracts the embedded child request and submits to C:

```c
// Machine B's submit implementation
void b_submit(void* state, kwork_t* work) {
    B_state_t* b = (B_state_t*)state;
    B_req_t* req = CONTAINER_OF(work, B_req_t, work);

    // Process B's part of the operation
    // ...

    // Prepare embedded child request
    req->c_req.work.requester = &B_machine;
    req->c_req.work.callback = b_c_completion_callback;
    req->c_req.work.op = C_OP_SEND;
    req->c_req.work.state = KWORK_STATE_DEAD;

    req->c_req.c_param1 = req->b_param1 * 2;

    // Submit to machine C
    b->machine_c->submit(b->machine_c->state, &req->c_req.work);

    work->state = KWORK_STATE_LIVE;
}
```

When C completes the work, it calls B's complete method:

```c
// Machine C's completion (internal or from lower layer)
void c_complete_work(C_state_t* c, kwork_t* work, kerr_t err) {
    // work->requester is B_machine
    work->requester->complete(work->requester->state, work, err);
}
```

Machine B's complete method processes C's completion and forwards to A:

```c
// Machine B's complete implementation
void b_complete(void* state, kwork_t* work, kerr_t err) {
    // work is &req->c_req.work
    C_req_t* c_req = CONTAINER_OF(work, C_req_t, work);

    // Get parent request
    B_req_t* b_req = CONTAINER_OF(c_req, B_req_t, c_req);

    // Process result from C
    // ...

    // Forward completion to A
    b_req->work.requester->complete(
        b_req->work.requester->state,
        &b_req->work,
        err
    );
}
```

### Key Properties

The async stack pattern provides:

1. **Zero allocation**: Full request depth allocated upfront by submitter
2. **Clear ownership**: Submitter owns the request structure
3. **Type safety**: Each machine defines its request type with embedded children
4. **Bounded depth**: Maximum delegation depth determined at compile time
5. **Cache locality**: Entire request stack in contiguous memory

### Like a Call Stack, But Async

The pattern mirrors a function call stack:

**Synchronous calls**:
```
A calls B()
  B's stack frame pushed
  B calls C()
    C's stack frame pushed
    C returns
  C's stack frame popped
  B returns
A's stack frame popped
```

**Async machine pattern**:
```
A allocates B_req_t (contains embedded C_req_t)
A submits to B
  B extracts B_req_t
  B submits embedded c_req to C
    C processes
    C calls B->complete()
  B processes C's completion
  B calls A->complete()
A processes B's completion
A invokes callback
```

The "async stack" is the chain of embedded requests. The requester chain (`work->requester`) forms the "return path."

### Multi-Level Example

For deeper stacks (A → B → C → D):

```c
// D's request type (leaf machine, no children)
typedef struct {
    kwork_t work;
    uint32_t d_param;
} D_req_t;

// C's request type (embeds D)
typedef struct {
    kwork_t work;
    uint32_t c_param;
    D_req_t d_req;  // Embedded
} C_req_t;

// B's request type (embeds C, which embeds D)
typedef struct {
    kwork_t work;
    uint32_t b_param;
    C_req_t c_req;  // Embedded (contains D)
} B_req_t;

// A allocates B_req_t (which contains full stack)
B_req_t req;  // Contains: work, b_param, c_req {work, c_param, d_req {work, d_param}}
```

The submitter (A) allocates space for the entire delegation chain. No allocation happens during delegation at runtime.

## 4. Operation Code Scoping

### Per-Machine Operation Spaces

Operation codes are unsigned 32-bit integers interpreted differently by each machine:

```c
// User machine operations (application-defined)
enum user_ops {
    USER_OP_FETCH_RESOURCE = 0,
    USER_OP_PROCESS_DATA = 1,
    USER_OP_SAVE_RESULT = 2,
};

// Runtime machine operations (kernel API)
enum runtime_ops {
    RUNTIME_OP_TIMER = 0,        // Same numeric value, different meaning!
    RUNTIME_OP_RNG_READ = 1,
    RUNTIME_OP_BLOCK_READ = 2,
    RUNTIME_OP_BLOCK_WRITE = 3,
    RUNTIME_OP_NET_SEND = 4,
    RUNTIME_OP_NET_RECV = 5,
};

// Platform machine operations (hardware abstraction)
enum platform_ops {
    PLATFORM_OP_TIMER_SETUP = 0,  // Same numeric value, different meaning!
    PLATFORM_OP_MMIO_READ = 1,
    PLATFORM_OP_MMIO_WRITE = 2,
    PLATFORM_OP_IRQ_REGISTER = 3,
};

// Network stack machine operations
enum netstack_ops {
    NETSTACK_OP_UDP_SEND = 0,     // Same numeric value, different meaning!
    NETSTACK_OP_UDP_RECV = 1,
    NETSTACK_OP_TCP_CONNECT = 2,
    NETSTACK_OP_TCP_SEND = 3,
};
```

Each machine interprets opcodes in its own namespace. This scoping enables:

- **Simplicity**: No global opcode registry required
- **Flexibility**: Machines define operations independently
- **Swappability**: Machines with same opcode contract can be swapped

### Operation Code Conventions

While opcodes are per-machine, conventions can standardize common operations:

```c
// Conventional opcode ranges (similar to IP address ranges)
// 0-255: Reserved for universal operations (optional)
#define KWORK_OP_TIMER         0
#define KWORK_OP_CANCEL        1
#define KWORK_OP_FLUSH         2

// 256-65535: Machine-specific operations
#define NETSTACK_OP_UDP_SEND   256
#define NETSTACK_OP_UDP_RECV   257
#define NETSTACK_OP_TCP_CONNECT 258

// 0x80000000-0xFFFFFFFF: Private/experimental
```

Conventions provide:
- Optional standardization without enforcement
- Easier debugging (recognizable opcode values)
- Partial compatibility between machines
- Room for private/experimental opcodes

But they're not required. Machines can use any opcodes they want.

### Operation Translation

When machine A delegates to B, it translates from its opcode space to B's opcode space:

```c
void runtime_submit(void* state, kwork_t* work) {
    runtime_t* rt = (runtime_t*)state;

    // work->op is in runtime opcode space
    switch (work->op) {
        case RUNTIME_OP_NET_SEND: {
            net_send_req_t* req = CONTAINER_OF(work, net_send_req_t, work);

            // Prepare embedded platform request
            req->platform_req.work.op = NETSTACK_OP_UDP_SEND;  // Translate!
            req->platform_req.work.requester = &runtime_machine;
            // ... set other fields

            // Submit to platform
            rt->platform->submit(rt->platform->state, &req->platform_req.work);
            break;
        }
        case RUNTIME_OP_TIMER: {
            // Handle directly (no delegation)
            handle_timer(rt, work);
            break;
        }
    }
}
```

The translation is explicit: A knows what operation it's requesting from B and maps its opcode to B's opcode.

## 5. Machine Composition

### Concrete State Types

Each machine has a concrete state type that holds its specific state:

```c
typedef struct {
    machine_t machine;     // Could embed or reference separately

    // Network stack state
    arp_cache_t arp_cache;
    route_table_t routes;
    ip_addr_t local_ip;
    mac_addr_t local_mac;

    // Delegate machine (concrete type or generic)
    machine_t* virtio_net;   // Generic: can swap VirtIO implementations
    // Or: virtio_net_t* virtio_net;  // Concrete: specific VirtIO machine

    // Pending work
    pending_arp_t pending_arp[MAX_PENDING_ARP];
} net_stack_t;
```

Machine methods receive `void* state` and cast to the concrete type:

```c
void net_stack_submit(void* state, kwork_t* work) {
    net_stack_t* net = (net_stack_t*)state;

    // Access net->arp_cache, net->virtio_net, etc.
}

void net_stack_tick(void* state) {
    net_stack_t* net = (net_stack_t*)state;

    // Process timeouts, retransmissions, etc.
}
```

### Delegate References

Machines hold references to delegate machines (children they submit work to):

**Generic delegate** (any machine with matching opcode contract):
```c
typedef struct {
    machine_t machine;
    machine_t* delegate;  // Generic: could be any compatible machine
} processor_t;
```

**Concrete delegate** (specific machine type):
```c
typedef struct {
    machine_t machine;
    virtio_blk_t* block_device;  // Concrete: knows it's VirtIO block
} file_system_t;
```

**Multiple delegates** (different subsystems):
```c
typedef struct {
    machine_t machine;
    machine_t* network_stack;
    machine_t* storage_stack;
    machine_t* rng_device;
} platform_t;
```

### Static Initialization

Machines are typically wired at initialization time:

```c
// Declare machine state structures
net_stack_t net_stack_state;
virtio_net_t virtio_net_state;
platform_t platform_state;

// Initialize machines
void system_init(void) {
    // Initialize VirtIO network device
    virtio_net_init(&virtio_net_state, pci_base_addr);

    // Initialize network stack, passing VirtIO machine reference
    net_stack_init(&net_stack_state, &virtio_net_state.machine);

    // Initialize platform, passing network stack reference
    platform_init(&platform_state, &net_stack_state.machine);
}

void net_stack_init(net_stack_t* net, machine_t* virtio_net) {
    memset(net, 0, sizeof(*net));

    // Set up machine interface
    net->machine.state = net;
    net->machine.submit = net_stack_submit;
    net->machine.complete = net_stack_complete;
    net->machine.query = net_stack_query;
    net->machine.tick = net_stack_tick;
    // ... other methods

    // Store delegate reference
    net->virtio_net = virtio_net;

    // Initialize network stack state
    arp_cache_init(&net->arp_cache);
    // ...
}
```

This provides:
- Clear initialization order
- Static memory allocation
- Explicit wiring at startup
- Testability (pass mock machines)

### Swappable Machines

Because machines expose a uniform interface, they can be swapped if they implement the same opcode contract:

```c
// Two different network stack implementations
extern machine_t full_tcp_ip_stack_machine;
extern machine_t minimal_udp_only_machine;

// Platform can use either
void platform_init(platform_t* p, machine_t* net_stack) {
    p->network_stack = net_stack;  // Could be either implementation
}

// Usage
platform_init(&platform, &full_tcp_ip_stack_machine);
// Or:
platform_init(&platform, &minimal_udp_only_machine);
```

Swappability requires:
- Same `machine_t` interface (guaranteed by pattern)
- Same operation code definitions (contract)
- Same request types (type compatibility)

## 6. Complete Message Flow Example

Let's trace a UDP send operation through a 4-layer machine stack:

```
User Machine
    ↓
Runtime Machine
    ↓
Platform Machine
    ↓
Network Stack Machine
    ↓
VirtIO Machine
```

### Request Type Definitions

```c
// VirtIO request (leaf, no children)
typedef struct {
    kwork_t work;
    uint8_t* frame_buffer;
    size_t frame_length;
} virtio_tx_req_t;

// Network stack request (embeds VirtIO)
typedef struct {
    kwork_t work;
    ip_addr_t dest_ip;
    uint16_t dest_port;
    uint8_t* payload;
    size_t payload_len;
    virtio_tx_req_t virtio_req;  // Embedded
} netstack_udp_send_req_t;

// Platform request (embeds network stack)
typedef struct {
    kwork_t work;
    // Platform-specific fields
    netstack_udp_send_req_t netstack_req;  // Embedded
} platform_net_send_req_t;

// Runtime request (embeds platform)
typedef struct {
    kwork_t work;
    // Runtime-specific fields
    platform_net_send_req_t platform_req;  // Embedded
} runtime_net_send_req_t;

// User request (embeds runtime)
typedef struct {
    kwork_t work;
    uint32_t user_context;
    runtime_net_send_req_t runtime_req;  // Embedded
} user_send_req_t;
```

### Step 1: User Submits Request

```c
// User machine code
void user_send_udp(user_machine_t* user, ip_addr_t dest, uint16_t port,
                   uint8_t* data, size_t len) {
    user_send_req_t* req = allocate_request(user);  // From user's pool

    req->work.requester = &user->machine;
    req->work.callback = user_send_complete;
    req->work.op = USER_OP_SEND_DATA;
    req->work.state = KWORK_STATE_DEAD;
    req->user_context = 42;

    // Set up embedded runtime request
    req->runtime_req.work.requester = &runtime_machine;
    req->runtime_req.work.op = RUNTIME_OP_NET_SEND;
    // ... fill in other fields

    // Submit to runtime
    runtime_machine.submit(runtime_machine.state, &req->runtime_req.work);
}
```

### Step 2: Runtime Receives and Delegates

```c
// Runtime machine submit
void runtime_submit(void* state, kwork_t* work) {
    runtime_t* rt = (runtime_t*)state;

    // work->op is RUNTIME_OP_NET_SEND
    runtime_net_send_req_t* req = CONTAINER_OF(work, runtime_net_send_req_t, work);

    // Set up embedded platform request
    req->platform_req.work.requester = &runtime_machine;
    req->platform_req.work.callback = runtime_net_send_callback;
    req->platform_req.work.op = PLATFORM_OP_NET_SEND;
    req->platform_req.work.state = KWORK_STATE_DEAD;

    // Submit to platform
    rt->platform->submit(rt->platform->state, &req->platform_req.work);

    work->state = KWORK_STATE_LIVE;
}
```

### Step 3: Platform Receives and Delegates

```c
// Platform machine submit
void platform_submit(void* state, kwork_t* work) {
    platform_t* plat = (platform_t*)state;

    // work->op is PLATFORM_OP_NET_SEND
    platform_net_send_req_t* req = CONTAINER_OF(work, platform_net_send_req_t, work);

    // Set up embedded network stack request
    req->netstack_req.work.requester = &platform_machine;
    req->netstack_req.work.callback = platform_netstack_callback;
    req->netstack_req.work.op = NETSTACK_OP_UDP_SEND;
    req->netstack_req.work.state = KWORK_STATE_DEAD;
    req->netstack_req.dest_ip = /* extract from request */;
    req->netstack_req.dest_port = /* extract from request */;
    // ...

    // Submit to network stack
    plat->netstack->submit(plat->netstack->state, &req->netstack_req.work);

    work->state = KWORK_STATE_LIVE;
}
```

### Step 4: Network Stack Receives and Delegates

```c
// Network stack machine submit
void netstack_submit(void* state, kwork_t* work) {
    net_stack_t* net = (net_stack_t*)state;

    // work->op is NETSTACK_OP_UDP_SEND
    netstack_udp_send_req_t* req = CONTAINER_OF(work, netstack_udp_send_req_t, work);

    // Check ARP cache for destination MAC
    mac_addr_t dest_mac;
    if (!arp_cache_lookup(&net->arp_cache, req->dest_ip, &dest_mac)) {
        // ARP cache miss - would initiate ARP resolution here
        // For simplicity, assume we have the MAC
    }

    // Build ethernet frame with UDP/IP/Ethernet headers
    uint8_t frame[1500];
    size_t frame_len = build_udp_frame(frame, req, dest_mac, net->local_mac);

    // Set up embedded VirtIO request
    req->virtio_req.work.requester = &netstack_machine;
    req->virtio_req.work.callback = netstack_virtio_callback;
    req->virtio_req.work.op = VIRTIO_OP_TX;
    req->virtio_req.work.state = KWORK_STATE_DEAD;
    req->virtio_req.frame_buffer = frame;
    req->virtio_req.frame_length = frame_len;

    // Submit to VirtIO
    net->virtio_net->submit(net->virtio_net->state, &req->virtio_req.work);

    work->state = KWORK_STATE_LIVE;
}
```

### Step 5: VirtIO Processes Request

```c
// VirtIO machine submit
void virtio_submit(void* state, kwork_t* work) {
    virtio_net_t* virtio = (virtio_net_t*)state;

    // work->op is VIRTIO_OP_TX
    virtio_tx_req_t* req = CONTAINER_OF(work, virtio_tx_req_t, work);

    // Add frame to virtqueue
    uint16_t desc_idx = virtqueue_alloc_desc(&virtio->tx_vq);
    virtqueue_set_buffer(&virtio->tx_vq, desc_idx,
                        req->frame_buffer, req->frame_length);
    virtqueue_add_avail(&virtio->tx_vq, desc_idx);

    // Store work pointer for later completion
    virtio->tx_work[desc_idx] = work;

    // Ring doorbell (notify device)
    virtio_notify(&virtio->tx_vq);

    work->state = KWORK_STATE_LIVE;
}
```

### Step 6: VirtIO Interrupt and Completion

```c
// VirtIO interrupt handler (called by hardware interrupt)
void virtio_irq_handler(void* context) {
    virtio_net_t* virtio = (virtio_net_t*)context;

    // Check used ring for completed descriptors
    while (virtqueue_has_used(&virtio->tx_vq)) {
        uint16_t desc_idx = virtqueue_get_used(&virtio->tx_vq);
        kwork_t* work = virtio->tx_work[desc_idx];

        // Free descriptor
        virtqueue_free_desc(&virtio->tx_vq, desc_idx);

        // Complete work (call requester's complete method)
        work->requester->complete(work->requester->state, work, KERR_OK);
    }
}
```

### Step 7: Network Stack Completion

```c
// Network stack complete method
void netstack_complete(void* state, kwork_t* work, kerr_t err) {
    net_stack_t* net = (net_stack_t*)state;

    // work is &req->virtio_req.work
    virtio_tx_req_t* virtio_req = CONTAINER_OF(work, virtio_tx_req_t, work);

    // Get parent request
    netstack_udp_send_req_t* netstack_req =
        CONTAINER_OF(virtio_req, netstack_udp_send_req_t, virtio_req);

    // Process VirtIO completion (nothing to do for simple send)

    // Forward completion to platform
    netstack_req->work.requester->complete(
        netstack_req->work.requester->state,
        &netstack_req->work,
        err
    );
}
```

### Step 8: Platform Completion

```c
// Platform complete method
void platform_complete(void* state, kwork_t* work, kerr_t err) {
    platform_t* plat = (platform_t*)state;

    // work is &req->netstack_req.work
    netstack_udp_send_req_t* netstack_req =
        CONTAINER_OF(work, netstack_udp_send_req_t, work);

    // Get parent request
    platform_net_send_req_t* platform_req =
        CONTAINER_OF(netstack_req, platform_net_send_req_t, netstack_req);

    // Forward completion to runtime
    platform_req->work.requester->complete(
        platform_req->work.requester->state,
        &platform_req->work,
        err
    );
}
```

### Step 9: Runtime Completion

```c
// Runtime complete method
void runtime_complete(void* state, kwork_t* work, kerr_t err) {
    runtime_t* rt = (runtime_t*)state;

    // work is &req->platform_req.work
    platform_net_send_req_t* platform_req =
        CONTAINER_OF(work, platform_net_send_req_t, work);

    // Get parent request
    runtime_net_send_req_t* runtime_req =
        CONTAINER_OF(platform_req, runtime_net_send_req_t, platform_req);

    // Mark work READY and enqueue for callback
    runtime_req->work.state = KWORK_STATE_READY;
    enqueue_ready_work(rt, &runtime_req->work);
}

// Later, in runtime tick()
void runtime_tick(void* state) {
    runtime_t* rt = (runtime_t*)state;

    // Process ready work queue
    kwork_t* work;
    while ((work = dequeue_ready_work(rt)) != NULL) {
        // Get user request
        runtime_net_send_req_t* runtime_req =
            CONTAINER_OF(work, runtime_net_send_req_t, work);
        user_send_req_t* user_req =
            CONTAINER_OF(runtime_req, user_send_req_t, runtime_req);

        // Invoke user callback
        user_req->work.callback(&user_req->work, KERR_OK);

        // Mark work DEAD
        user_req->work.state = KWORK_STATE_DEAD;
    }
}
```

### Trace Summary

```
[User allocates user_send_req_t which contains the entire nested structure]
  ↓ submit
[Runtime receives, delegates to platform]
  ↓ submit
[Platform receives, delegates to network stack]
  ↓ submit
[Network stack builds UDP/IP/Ethernet, delegates to VirtIO]
  ↓ submit
[VirtIO adds to virtqueue, rings doorbell]
  ↓ (hardware processes)
[VirtIO interrupt: calls netstack->complete()]
  ↓ complete
[Network stack: calls platform->complete()]
  ↓ complete
[Platform: calls runtime->complete()]
  ↓ complete
[Runtime: enqueues work as READY]
  ↓ tick
[Runtime: invokes user callback]
  ↓ callback
[User: work complete, marks DEAD]
```

**Key observations**:
- Single allocation by user contains entire request stack
- Each layer extracts its request via `CONTAINER_OF`
- Each layer submits embedded child request
- Completion propagates back up the requester chain
- Zero dynamic allocation during delegation

## 7. Synchronous Operations: query()

### The Performance Problem

Some operations are inherently synchronous and performance-critical:
- MMIO register reads/writes
- PCI configuration space access
- Quick lookups in tables/caches
- Synchronous queries (e.g., "what's the current state?")

Using async submit/complete for these adds unnecessary overhead:
- Allocate request structure
- Submit work item
- State transitions
- Completion callback

For a simple MMIO read, this is wasteful.

### The query() Method

The `query()` method provides a synchronous fast path:

```c
// Synchronous operation - returns immediately
void (*query)(void* state, kwork_t* work);
```

Unlike `submit()`:
- Returns immediately with result in work structure
- No state transitions
- No completion callback invoked
- No queuing or deferred processing

### Usage Pattern

```c
// Request type for MMIO read
typedef struct {
    kwork_t work;        // work.op = MMIO_OP_READ32
    uintptr_t address;   // Input
    uint32_t value;      // Output
} mmio_read_req_t;

// Caller code
mmio_read_req_t req;
req.work.op = MMIO_OP_READ32;
req.address = device_base + OFFSET_STATUS;

// Synchronous query - returns immediately
platform_machine.query(platform_machine.state, &req.work);

// Result available immediately
uint32_t status = req.value;
```

### Implementation Example

```c
void platform_query(void* state, kwork_t* work) {
    platform_t* plat = (platform_t*)state;

    switch (work->op) {
        case MMIO_OP_READ32: {
            mmio_read_req_t* req = CONTAINER_OF(work, mmio_read_req_t, work);
            req->value = *(volatile uint32_t*)req->address;
            break;
        }

        case MMIO_OP_WRITE32: {
            mmio_write_req_t* req = CONTAINER_OF(work, mmio_write_req_t, work);
            *(volatile uint32_t*)req->address = req->value;
            break;
        }

        case PCI_OP_CONFIG_READ: {
            pci_config_req_t* req = CONTAINER_OF(work, pci_config_req_t, work);
            req->value = pci_config_read(plat, req->bus, req->dev, req->func, req->offset);
            break;
        }
    }

    // No state transitions, no callbacks
}
```

### Separate Opcode Spaces

Sync and async operations typically have separate opcode spaces to avoid confusion:

```c
// Async operations
enum platform_async_ops {
    PLATFORM_OP_NET_SEND = 0,
    PLATFORM_OP_BLOCK_READ = 1,
    PLATFORM_OP_TIMER = 2,
};

// Sync operations
enum platform_sync_ops {
    PLATFORM_QUERY_MMIO_READ = 0,    // Different namespace!
    PLATFORM_QUERY_MMIO_WRITE = 1,
    PLATFORM_QUERY_PCI_CONFIG = 2,
    PLATFORM_QUERY_TIME = 3,
};
```

This prevents accidentally using `submit()` for sync operations or `query()` for async operations.

### When to Use query()

Use `query()` for:
- ✅ MMIO reads/writes
- ✅ PCI configuration space access
- ✅ Reading current time
- ✅ Quick table lookups
- ✅ State queries

Use `submit()` for:
- ✅ Network I/O
- ✅ Disk I/O
- ✅ Timers (async wait)
- ✅ Any operation that may block
- ✅ Any operation with non-trivial latency

### Hybrid Operations

Some machines support both sync and async versions of operations:

```c
// Async disk read (may take milliseconds)
disk_read_req_t read_req;
read_req.work.op = DISK_OP_READ;
storage->submit(storage->state, &read_req.work);

// Sync query: is disk idle?
disk_status_req_t status_req;
status_req.work.op = DISK_QUERY_STATUS;
storage->query(storage->state, &status_req.work);
bool idle = status_req.idle;  // Available immediately
```

## 8. Completion Semantics

### The complete() Method

When a machine finishes processing work (either internally or by receiving completion from a child machine), it calls the requester's `complete()` method:

```c
work->requester->complete(work->requester->state, work, err);
```

The `complete()` method is responsible for:
1. Processing the result
2. Updating machine state
3. Deciding whether to forward completion or do more work
4. Eventually invoking the callback

### Three Completion Patterns

#### Pattern 1: Process and Forward

The machine processes the completion, then forwards it upstream:

```c
void machine_complete(void* state, kwork_t* work, kerr_t err) {
    machine_t* m = (machine_t*)state;

    // Extract request
    child_req_t* child_req = CONTAINER_OF(work, child_req_t, work);
    parent_req_t* parent_req = CONTAINER_OF(child_req, parent_req_t, child_req);

    // Process child completion
    if (err == KERR_OK) {
        // Extract results from child_req
        parent_req->result = child_req->output_value;
    }

    // Forward to parent requester
    parent_req->work.requester->complete(
        parent_req->work.requester->state,
        &parent_req->work,
        err
    );
}
```

#### Pattern 2: Passthrough

The machine immediately forwards completion without processing:

```c
void machine_complete(void* state, kwork_t* work, kerr_t err) {
    // Extract parent request
    child_req_t* child_req = CONTAINER_OF(work, child_req_t, work);
    parent_req_t* parent_req = CONTAINER_OF(child_req, parent_req_t, child_req);

    // Immediately forward (no processing needed)
    parent_req->work.requester->complete(
        parent_req->work.requester->state,
        &parent_req->work,
        err
    );
}
```

Use this pattern when the machine is just routing work and doesn't need to process results.

#### Pattern 3: Multi-Step Operations

The machine does more work before completing:

```c
void netstack_complete(void* state, kwork_t* work, kerr_t err) {
    net_stack_t* net = (net_stack_t*)state;

    // Extract request
    virtio_req_t* virtio_req = CONTAINER_OF(work, virtio_req_t, work);
    arp_req_t* arp_req = CONTAINER_OF(virtio_req, arp_req_t, virtio_req);

    if (err == KERR_OK && work->op == NETSTACK_OP_ARP_RESPONSE) {
        // ARP request completed successfully
        // Update ARP cache
        arp_cache_insert(&net->arp_cache, arp_req->ip, arp_req->mac);

        // Resume any work waiting for this ARP entry
        resume_pending_work(net, arp_req->ip);

        // DON'T forward completion - this was an internal operation
        return;
    }

    // For user-facing operations, forward completion
    arp_req->work.requester->complete(
        arp_req->work.requester->state,
        &arp_req->work,
        err
    );
}
```

### Deferred Callbacks

The machine that receives the final completion (typically the runtime/kernel machine) may defer callback invocation:

```c
void runtime_complete(void* state, kwork_t* work, kerr_t err) {
    runtime_t* rt = (runtime_t*)state;

    // Don't invoke callback immediately
    // Instead, mark work READY and enqueue
    work->state = KWORK_STATE_READY;
    work->completion_err = err;
    enqueue_ready(rt, work);
}

void runtime_tick(void* state) {
    runtime_t* rt = (runtime_t*)state;

    // Process ready queue
    kwork_t* work;
    while ((work = dequeue_ready(rt)) != NULL) {
        // Invoke callback now
        work->callback(work, work->completion_err);
        work->state = KWORK_STATE_DEAD;
    }
}
```

This batching provides:
- Callback invocation outside interrupt context
- Consistent callback environment
- Batch processing opportunities
- Stack depth management

### Error Propagation

Errors propagate up the completion chain:

```c
// VirtIO fails
virtio_complete(..., work, KERR_IO_ERROR);
  ↓
// Network stack receives error
netstack_complete(..., work, KERR_IO_ERROR);
  // Could translate error code if needed
  forward_complete(..., work, KERR_NETWORK_ERROR);
  ↓
// Platform receives error
platform_complete(..., work, KERR_NETWORK_ERROR);
  forward_complete(..., work, KERR_NETWORK_ERROR);
  ↓
// User callback receives error
user_callback(work, KERR_NETWORK_ERROR);
```

Machines can:
- Forward errors unchanged
- Translate error codes (device error → network error)
- Log errors for debugging
- Retry operations on certain errors

## 9. Cancellation

### The cancel() Method

The `cancel()` method requests cancellation of in-flight work:

```c
void (*cancel)(void* state, kwork_t* work);
```

### Best-Effort Semantics

Cancellation is always **best-effort**:
- Some operations can be cancelled (e.g., pending network send in queue)
- Some operations cannot be cancelled (e.g., committed disk write)
- Work always completes eventually (with success, error, or cancellation error)

```c
// Request cancellation
network_machine.cancel(network_machine.state, &send_req.work);

// Work will complete later with one of:
// - KERR_OK (completed before cancellation)
// - KERR_CANCELLED (successfully cancelled)
// - KERR_SOME_ERROR (failed for other reason)
```

### Propagation

Cancellation propagates down the machine stack:

```c
void machine_cancel(void* state, kwork_t* work) {
    machine_t* m = (machine_t*)state;

    // Extract request
    parent_req_t* parent_req = CONTAINER_OF(work, parent_req_t, work);

    // Check if child work has been submitted
    if (parent_req->child_req.work.state == KWORK_STATE_LIVE) {
        // Cancel child work
        m->child_machine->cancel(
            m->child_machine->state,
            &parent_req->child_req.work
        );
    } else if (parent_req->work.state == KWORK_STATE_SUBMIT_REQUESTED) {
        // Work hasn't been submitted yet - can cancel immediately
        parent_req->work.state = KWORK_STATE_READY;
        parent_req->work.requester->complete(
            parent_req->work.requester->state,
            &parent_req->work,
            KERR_CANCELLED
        );
    }
}
```

### Implementation Examples

**Cancellable operation** (work in queue):
```c
void virtio_cancel(void* state, kwork_t* work) {
    virtio_net_t* virtio = (virtio_net_t*)state;
    virtio_tx_req_t* req = CONTAINER_OF(work, virtio_tx_req_t, work);

    // Find work in pending queue
    if (remove_from_pending_queue(&virtio->tx_queue, work)) {
        // Successfully removed - complete with cancellation error
        work->requester->complete(
            work->requester->state,
            work,
            KERR_CANCELLED
        );
    }
    // If not in queue, it's already submitted to hardware - can't cancel
}
```

**Non-cancellable operation** (committed to hardware):
```c
void disk_cancel(void* state, kwork_t* work) {
    // Disk writes already submitted to hardware cannot be cancelled
    // Operation will complete normally
    // (We could track cancellation request and return KERR_CANCELLED
    //  when it completes, even though the write happened)
}
```

## 10. Swappability

### What Makes Machines Swappable?

Three requirements enable machine swapping:

1. **Standard Interface**: All machines expose the same `machine_t` interface
2. **Opcode Contract**: Machines implement the same operation codes
3. **Type Compatibility**: Machines accept/produce the same request types

### Standard Interface (Guaranteed)

The `machine_t` interface is uniform across all machines:

```c
machine_t* get_network_stack(int variant) {
    switch (variant) {
        case FULL_TCP_IP:
            return &full_tcp_ip_machine;
        case UDP_ONLY:
            return &udp_only_machine;
        case MINIMAL:
            return &minimal_netstack_machine;
    }
}

// All implement machine_t interface
platform_init(&platform, get_network_stack(UDP_ONLY));
```

### Opcode Contract

Machines that implement the same opcodes can be swapped:

```c
// Network stack opcode contract
enum netstack_ops {
    NETSTACK_OP_UDP_SEND = 0,
    NETSTACK_OP_UDP_RECV = 1,
    NETSTACK_OP_TCP_CONNECT = 2,    // Optional
    NETSTACK_OP_TCP_SEND = 3,       // Optional
    // ...
};

// Minimal implementation (UDP only)
void minimal_netstack_submit(void* state, kwork_t* work) {
    switch (work->op) {
        case NETSTACK_OP_UDP_SEND:
            handle_udp_send(state, work);
            break;
        case NETSTACK_OP_UDP_RECV:
            handle_udp_recv(state, work);
            break;
        default:
            // Unsupported operation
            work->requester->complete(work->requester->state,
                                     work, KERR_NOT_SUPPORTED);
            break;
    }
}

// Full implementation (TCP + UDP)
void full_netstack_submit(void* state, kwork_t* work) {
    switch (work->op) {
        case NETSTACK_OP_UDP_SEND:
            handle_udp_send(state, work);
            break;
        case NETSTACK_OP_UDP_RECV:
            handle_udp_recv(state, work);
            break;
        case NETSTACK_OP_TCP_CONNECT:
            handle_tcp_connect(state, work);
            break;
        case NETSTACK_OP_TCP_SEND:
            handle_tcp_send(state, work);
            break;
        // ...
    }
}
```

Partial implementations return `KERR_NOT_SUPPORTED` for unsupported opcodes.

### Type Compatibility

Request types must match:

```c
// Network stack request type (shared contract)
typedef struct {
    kwork_t work;
    ip_addr_t dest_ip;
    uint16_t dest_port;
    uint8_t* payload;
    size_t payload_len;
    // Implementation-specific fields embedded here or in private area
} netstack_send_req_t;
```

Both minimal and full implementations accept `netstack_send_req_t`. Implementation-specific state can be stored in:
- Additional fields (if caller knows about them)
- Opaque trailing data (implementation allocates extra space)
- Separate per-machine state structures

### Swapping at Runtime

Machines can be swapped at initialization:

```c
void platform_init(platform_t* p, machine_t* netstack) {
    p->netstack = netstack;  // Store generic machine pointer
}

// Choose at startup
if (config.minimal_network) {
    platform_init(&platform, &minimal_netstack_machine);
} else {
    platform_init(&platform, &full_tcp_ip_machine);
}
```

Or even dynamically (with care):

```c
void platform_swap_netstack(platform_t* p, machine_t* new_netstack) {
    // Ensure no work in flight
    assert(no_pending_work(p->netstack));

    // Swap
    machine_t* old = p->netstack;
    p->netstack = new_netstack;

    // Clean up old machine if needed
}
```

### Example: Storage Backends

Different storage implementations can be swapped:

```c
// Storage opcode contract
enum storage_ops {
    STORAGE_OP_READ = 0,
    STORAGE_OP_WRITE = 1,
    STORAGE_OP_FLUSH = 2,
};

// Implementations
extern machine_t virtio_blk_machine;   // VirtIO block device
extern machine_t nvme_machine;         // NVMe device
extern machine_t ram_disk_machine;     // RAM disk for testing
extern machine_t file_backed_machine;  // File-backed storage (hosted)

// Platform chooses at init
void platform_init_storage(platform_t* p, machine_t* storage) {
    p->storage = storage;
}

// All implement same opcode contract, can be swapped
```

## 11. Benefits of the Machine Pattern

### Separation of Concerns

Each machine has one responsibility:
- User machine: Application logic
- Runtime machine: Work queue management, timer heap
- Platform machine: Hardware abstraction
- Network stack machine: Protocol implementation
- VirtIO machine: Device driver

Clear boundaries make code easier to understand, test, and modify.

### Platform Flexibility

Same user code runs on different platforms:

**Bare-metal** (deep stack):
```
User → Runtime → Platform → Network Stack → VirtIO → Hardware
```

**Hosted** (shallow stack):
```
User → Runtime → Platform → OS BSD Sockets
```

Platform chooses its delegation depth based on available primitives. User code unchanged.

### Code Reuse

Machines can be shared across platforms:
- Network stack machine used by ARM, x64, RISC-V bare-metal platforms
- VirtIO drivers shared across architectures
- Runtime machine completely platform-independent

### Zero-Allocation Performance

The async stack pattern eliminates allocation during delegation:
- User allocates request once
- Request contains space for entire delegation chain
- No malloc/free in hot path
- Predictable memory usage

### Composability

Machines stack arbitrarily deep:
- Insert protocol layers (UDP → TCP → TLS)
- Add filtering (firewall machine between netstack and device)
- Add instrumentation (logging machine wrapping any other machine)
- Build complex systems from simple components

### Testability

Uniform interface enables testing:

```c
// Mock VirtIO machine for testing network stack
machine_t mock_virtio = {
    .state = &mock_state,
    .submit = mock_virtio_submit,
    .complete = mock_virtio_complete,
    // ...
};

// Test network stack in isolation
net_stack_init(&netstack, &mock_virtio);
```

Each machine can be tested independently with mocks for its dependencies.

### Traceability

The requester chain forms a trace:

```c
void trace_work(kwork_t* work) {
    printf("Work trace:\n");
    printf("  Current work: op=%u state=%u\n", work->op, work->state);
    printf("  Requester: %s\n", work->requester->name);

    // Can walk up requester chain (if we store parent work pointers)
    // Can emit trace events to Perfetto or similar tool
}
```

Instrumentation at each machine enables rich debugging and profiling.

### Performance: Sync/Async Separation

Critical operations use `query()` for immediate results:
- MMIO reads: no allocation, no state machine overhead
- PCI config: simple function call semantics
- Quick lookups: no callback complexity

Async operations use `submit()`/`complete()` only when needed.

---

# Part 2: Relation to VMOS

## 12. Current VMOS Architecture

VMOS demonstrates the machine pattern conceptually, though not with the full abstraction presented in Part 1.

### Three-Layer Stack

VMOS has three layers:

```
User Layer (user.h)
    ↓ ksubmit()
Runtime Layer (kernel.h, kernel.c)
    ↓ platform_submit()
Platform Layer (platform.h, platform_impl.h)
```

### User Layer

Applications use `kapi.h` to submit asynchronous work:

```c
// User code
krng_req_t rng_req;
kwork_init(&rng_req.work, KWORK_OP_RNG_READ, ctx, callback, 0);
rng_req.buffer = buffer;
rng_req.length = 32;
ksubmit(kernel, &rng_req.work);

// Later, callback invoked
void callback(kwork_t* work, kerr_t err) {
    krng_req_t* req = CONTAINER_OF(work, krng_req_t, work);
    // buffer contains random bytes
}
```

This is the machine pattern's `submit()` + callback.

### Runtime Layer

The kernel (`kernel.c`) manages work queue state machine:

```c
// Work states
DEAD → SUBMIT_REQUESTED → LIVE → READY → DEAD

// Main loop
void kmain_step(kernel_t* k) {
    kmain_tick(k, current_time);           // Process work, invoke callbacks
    ktime_t timeout = kmain_next_delay(k); // Next timer deadline
    current_time = platform_wfi(&k->platform, timeout);  // Wait for events
}
```

`kmain_tick()` is equivalent to `machine.tick()`.

`platform_wfi()` is equivalent to `machine.wait()`.

### Platform Layer

Platform implements `platform.h` contract:

```c
void platform_submit(platform_t* p, kwork_t* submissions, kwork_t* cancellations);
void platform_tick(platform_t* p, kernel_t* k);
ktime_t platform_wfi(platform_t* p, ktime_t timeout_ns);
```

Platform delegates to devices (VirtIO RNG, VirtIO block, VirtIO net) and calls `kplatform_complete_work()` when operations complete.

### Work Completion

Platforms complete work via:

```c
void kplatform_complete_work(kernel_t* k, kwork_t* work, kerr_t err);
```

This is equivalent to:

```c
work->requester->complete(work->requester->state, work, err);
```

The kernel transitions work to READY and later invokes the callback.

### Already Has Machine Pattern Elements

VMOS already demonstrates:
- ✅ Async work submission (`ksubmit`)
- ✅ Work state machine (DEAD/SUBMIT/LIVE/READY)
- ✅ Completion callback
- ✅ Operation code scoping (KWORK_OP_*)
- ✅ Embedded platform state in requests (`kwork.platform` field)

Missing from full pattern:
- ❌ Uniform `machine_t` interface
- ❌ Explicit `requester` field
- ❌ `query()` for sync operations
- ❌ Standardized sublayer interface

## 13. How VMOS Demonstrates the Pattern

### User as Machine

The user layer can be viewed as a machine:

```c
machine_t user_machine = {
    .state = &user_state,
    .submit = user_submit,        // User submits work to runtime
    .complete = user_complete,    // Runtime notifies user of completion
    .tick = user_tick,            // User processes events
    // ...
};
```

Users submit work via `ksubmit()` which internally calls `runtime_machine.submit()`.

### Runtime as Machine

The kernel/runtime layer is a machine:

```c
machine_t runtime_machine = {
    .state = &kernel,
    .submit = kernel_submit,      // Accept work from user
    .complete = kernel_complete,  // Platform notifies runtime
    .tick = kmain_tick,           // Process work, invoke callbacks
    .next_delay = kmain_next_delay,
    .wait = platform_wfi,         // Delegate to platform
    // ...
};
```

Runtime delegates to platform via `platform_submit()`.

### Platform as Machine

Each platform implementation is a machine:

```c
machine_t arm64_platform_machine = {
    .state = &platform,
    .submit = platform_submit,    // Accept work from runtime
    .complete = platform_complete,// Delegates complete to runtime
    .tick = platform_tick,        // Process IRQ ring, deferred work
    .query = platform_query,      // MMIO, PCI config (if added)
    // ...
};
```

Platform delegates to devices (VirtIO).

### Embedded Platform State

VMOS requests already use the async stack pattern via `platform` field:

```c
struct kwork {
    // ...
    uint32_t platform[16];  // Platform-specific state (64 bytes)
};
```

This is similar to embedding child requests, though less structured. Each platform can use this space for device-specific state.

### Operation Code Scoping

VMOS opcodes are runtime-layer codes:

```c
enum {
    KWORK_OP_TIMER = 0,
    KWORK_OP_RNG_READ = 1,
    KWORK_OP_BLOCK_READ = 2,
    KWORK_OP_BLOCK_WRITE = 3,
    KWORK_OP_NET_SEND = 4,
    KWORK_OP_NET_RECV = 5,
};
```

These are interpreted by the runtime machine. Platform and device layers may use different opcodes internally (though currently they just pass through runtime opcodes).

## 14. Potential Evolution

VMOS could fully embrace the machine pattern with these changes:

### Add machine_t Interface

Extract the machine abstraction:

```c
// New: kernel/machine.h
typedef struct machine {
    void* state;
    void (*submit)(void* state, kwork_t* work);
    void (*cancel)(void* state, kwork_t* work);
    void (*complete)(void* state, kwork_t* work, kerr_t err);
    void (*query)(void* state, kwork_t* work);
    void (*tick)(void* state);
    ktime_t (*next_delay)(void* state);
    ktime_t (*wait)(void* state, ktime_t timeout);
} machine_t;
```

### Add requester Field

Add to `kwork_t`:

```c
struct kwork {
    machine_t* requester;  // Machine that submitted this work
    void (*callback)(kwork_t*, kerr_t);
    uint32_t op;
    uint32_t state;
    // ...
};
```

Change completion from:

```c
kplatform_complete_work(kernel, work, err);
```

To:

```c
work->requester->complete(work->requester->state, work, err);
```

### Add query() for Sync Ops

Add synchronous operations:

```c
// MMIO read (currently in platform.h, but could use machine pattern)
platform_mmio_read32(addr);  // Current

// Machine pattern
mmio_read_req_t req = {
    .work.op = PLATFORM_QUERY_MMIO_READ32,
    .address = addr,
};
platform_machine.query(platform_machine.state, &req.work);
uint32_t value = req.value;
```

Benefit: Uniform interface, easier to mock for testing.

Cost: More verbose for simple operations.

Trade-off: Could keep direct functions for common ops, use query() for less common ones.

### Standardize Device Interfaces

VirtIO and other devices could expose `machine_t` interface:

```c
// Current: device-specific functions
virtio_rng_submit_work(device, work, kernel);

// Machine pattern
machine_t virtio_rng_machine = {
    .state = &virtio_rng_device,
    .submit = virtio_rng_submit,
    .complete = virtio_rng_complete,
    // ...
};

platform->rng = &virtio_rng_machine;
platform->rng->submit(platform->rng->state, work);
```

Benefits:
- Uniform device interface
- Easy to swap devices (VirtIO RNG vs hardware RNG)
- Testability (mock devices)

### Async Stack for Devices

Currently, platform passes work directly to devices. With async stack pattern:

```c
typedef struct {
    kwork_t work;              // work.requester = platform
    // Platform-specific fields
    virtio_rng_req_t rng_req;  // Embedded
} platform_rng_req_t;

// Platform extracts and submits embedded request
void platform_submit(void* state, kwork_t* work) {
    platform_t* p = (platform_t*)state;
    platform_rng_req_t* req = CONTAINER_OF(work, platform_rng_req_t, work);

    req->rng_req.work.requester = &platform_machine;
    req->rng_req.work.op = VIRTIO_RNG_OP_READ;

    p->rng->submit(p->rng->state, &req->rng_req.work);
}
```

### Network Stack as Machine

For bare-metal networking, add network stack machine between platform and VirtIO:

```c
machine_t netstack_machine = {
    .state = &netstack_state,
    .submit = netstack_submit,
    // ...
};

// Platform delegates network ops to netstack
void platform_submit(void* state, kwork_t* work) {
    platform_t* p = (platform_t*)state;
    if (work->op == KWORK_OP_NET_SEND) {
        // Delegate to network stack
        p->netstack->submit(p->netstack->state, work);
    }
}

// Network stack delegates to VirtIO net
void netstack_submit(void* state, kwork_t* work) {
    netstack_t* net = (netstack_t*)state;
    // Build UDP/IP/Ethernet headers
    // Submit to VirtIO
    net->virtio_net->submit(net->virtio_net->state, work);
}
```

This would enable UDP/TCP support on bare-metal platforms.

## 15. Example: Network Path Evolution

### Current VMOS (No Bare-Metal Networking)

```
User
  ↓ ksubmit(KWORK_OP_NET_SEND)
Runtime (kernel.c)
  ↓ platform_submit()
Platform (platform_virtio.c)
  ↓ virtio_net_submit_work()
VirtIO Net Device
```

Network sends are not yet fully implemented. Platform would need to:
- Build ethernet frames (no UDP/IP stack)
- Submit raw frames to VirtIO

### With Machine Pattern (Bare-Metal UDP Stack)

```
User
  ↓ machine.submit(USER_OP_SEND_DATA)
  ↓ [allocates user_req_t containing full stack]
Runtime
  ↓ machine.submit(RUNTIME_OP_NET_SEND)
Platform
  ↓ machine.submit(NETSTACK_OP_UDP_SEND)
Network Stack Machine (NEW)
  ↓ Build UDP/IP/Ethernet headers
  ↓ machine.submit(VIRTIO_OP_TX)
VirtIO Machine
  ↓ Add to virtqueue, ring doorbell
  ← Interrupt
  ↑ complete(KERR_OK)
Network Stack
  ↑ complete(KERR_OK)
Platform
  ↑ complete(KERR_OK)
Runtime
  ↑ complete(KERR_OK), invoke callback
User
```

Network stack machine handles:
- ARP resolution
- UDP header construction
- IP header construction (checksum, TTL, fragmentation)
- Ethernet header construction

### Hosted Platform (macOS, Linux)

```
User
  ↓ machine.submit(USER_OP_SEND_DATA)
  ↓ [allocates user_req_t with embedded runtime_req_t]
Runtime
  ↓ machine.submit(RUNTIME_OP_NET_SEND)
  ↓ [embedded platform_req_t]
Platform (Hosted)
  ↓ Direct socket I/O
  ↓ sendto(sock, buf, len, dest_addr)
  ← OS handles UDP/IP/Ethernet/ARP
  ↑ complete(KERR_OK)
Runtime
  ↑ complete(KERR_OK), invoke callback
User
```

Hosted platform handles UDP directly with OS sockets. No network stack machine needed.

### Same User Code, Different Depths

User code identical on both platforms:

```c
// User code (same everywhere)
net_send_req_t req;
req.work.requester = &user_machine;
req.work.callback = send_complete;
req.work.op = RUNTIME_OP_NET_SEND;
req.dest_ip = ip_addr("192.168.1.1");
req.dest_port = 8080;
req.payload = data;
req.payload_len = len;

runtime_machine.submit(runtime_machine.state, &req.work);
```

Platform chooses delegation depth:
- Bare-metal: delegate to network stack → VirtIO (deep)
- Hosted: handle directly with OS (shallow)

---

# Part 3: Application Guide

## 16. Designing Machine Stacks

### Identify Boundaries

Look for natural layers in your system:
- User/application logic
- Framework/runtime (event loop, work queue)
- Platform abstraction (OS or hardware layer)
- Protocol stacks (network, storage, etc.)
- Device drivers

Each layer boundary is a candidate for a machine interface.

### Define Operation Spaces

For each machine, define its operations:

```c
// Example: Storage machine
enum storage_ops {
    STORAGE_OP_READ = 0,      // Read blocks
    STORAGE_OP_WRITE = 1,     // Write blocks
    STORAGE_OP_FLUSH = 2,     // Flush caches
    STORAGE_OP_TRIM = 3,      // TRIM/DISCARD
    STORAGE_OP_GET_INFO = 4,  // Query capacity, block size
};
```

Include both async operations (submit) and sync operations (query) as appropriate.

### Choose Delegation Strategy

For each operation, decide:
- **Direct handling**: Machine has primitives to handle directly
- **Delegation**: Machine translates and delegates to child machine

Example:

```c
void platform_submit(void* state, kwork_t* work) {
    platform_t* p = (platform_t*)state;

    switch (work->op) {
        case PLATFORM_OP_RNG_READ:
            // Direct handling: arc4random_buf() on macOS
            handle_rng_directly(p, work);
            break;

        case PLATFORM_OP_NET_SEND:
            // Delegation: need network stack on bare-metal
            p->netstack->submit(p->netstack->state, work);
            break;
    }
}
```

### Design Request Types

Define request types with embedded children:

```c
// Leaf request (no children)
typedef struct {
    kwork_t work;
    uint8_t* buffer;
    size_t length;
} device_io_req_t;

// Parent request (embeds child)
typedef struct {
    kwork_t work;
    uint32_t logical_block;
    uint8_t* buffer;
    device_io_req_t device_req;  // Embedded
} storage_read_req_t;
```

Embed requests for the full delegation depth you expect.

### Decide on Concrete vs Generic Delegates

**Concrete** (faster, less flexible):
```c
typedef struct {
    machine_t machine;
    virtio_blk_t* block_device;  // Concrete type
} platform_t;
```

**Generic** (slower, more flexible):
```c
typedef struct {
    machine_t machine;
    machine_t* block_device;  // Generic machine
} platform_t;
```

Use concrete when:
- Performance critical
- Implementation unlikely to change
- Tight integration needed

Use generic when:
- Want swappability
- Multiple implementations exist
- Testing with mocks

## 17. Implementation Checklist

### 1. Define Concrete State Type

```c
typedef struct {
    machine_t machine;  // Interface (or separate)

    // Machine-specific state
    int my_state_field;
    buffer_pool_t buffers;

    // Delegate references
    machine_t* child_machine;
} my_machine_t;
```

### 2. Implement Machine Interface

```c
void my_machine_submit(void* state, kwork_t* work) {
    my_machine_t* m = (my_machine_t*)state;
    // Handle or delegate work
}

void my_machine_complete(void* state, kwork_t* work, kerr_t err) {
    my_machine_t* m = (my_machine_t*)state;
    // Process completion, forward upstream
}

void my_machine_tick(void* state) {
    my_machine_t* m = (my_machine_t*)state;
    // Process timeouts, deferred work
}
// ... other methods
```

### 3. Define Request Types

```c
typedef struct {
    kwork_t work;
    // Request parameters
    uint32_t param1;
    // Embedded child request
    child_req_t child_req;
} my_req_t;
```

### 4. Implement Delegation Logic

```c
void my_machine_submit(void* state, kwork_t* work) {
    my_machine_t* m = (my_machine_t*)state;
    my_req_t* req = CONTAINER_OF(work, my_req_t, work);

    // Prepare embedded child request
    req->child_req.work.requester = &my_machine;
    req->child_req.work.callback = my_child_callback;
    req->child_req.work.op = CHILD_OP_SOMETHING;

    // Submit to child
    m->child_machine->submit(m->child_machine->state, &req->child_req.work);
}
```

### 5. Handle Completion

```c
void my_machine_complete(void* state, kwork_t* work, kerr_t err) {
    // Extract request
    child_req_t* child_req = CONTAINER_OF(work, child_req_t, work);
    my_req_t* my_req = CONTAINER_OF(child_req, my_req_t, child_req);

    // Process result
    if (err == KERR_OK) {
        // Extract results from child_req
    }

    // Forward to parent
    my_req->work.requester->complete(
        my_req->work.requester->state,
        &my_req->work,
        err
    );
}
```

### 6. Initialize Machine

```c
void my_machine_init(my_machine_t* m, machine_t* child) {
    memset(m, 0, sizeof(*m));

    // Set up interface
    m->machine.state = m;
    m->machine.submit = my_machine_submit;
    m->machine.complete = my_machine_complete;
    m->machine.query = my_machine_query;
    m->machine.tick = my_machine_tick;
    m->machine.next_delay = my_machine_next_delay;
    m->machine.wait = my_machine_wait;
    m->machine.cancel = my_machine_cancel;

    // Store child reference
    m->child_machine = child;

    // Initialize state
    init_my_state(m);
}
```

### 7. Test in Isolation

```c
// Create mock child machine
machine_t mock_child = {
    .state = &mock_state,
    .submit = mock_submit,
    .complete = mock_complete,
};

// Initialize machine with mock
my_machine_t machine;
my_machine_init(&machine, &mock_child);

// Test
my_req_t req;
req.work.requester = &test_machine;
req.work.callback = test_callback;
machine.machine.submit(&machine, &req.work);

// Verify mock was called correctly
assert(mock_submit_called);
```

## 18. Performance Considerations

### Use query() for Hot Paths

Synchronous operations should use `query()`:

```c
// BAD: Using submit() for MMIO read
mmio_read_req_t req;
req.work.op = MMIO_OP_READ;
machine.submit(&machine, &req.work);
// ... wait for callback ...

// GOOD: Using query() for MMIO read
mmio_read_req_t req;
req.work.op = MMIO_QUERY_READ;
machine.query(&machine, &req.work);
uint32_t value = req.value;  // Immediate
```

### Async Stack Eliminates Allocation

Pre-allocate full delegation depth:

```c
// User allocates once
typedef struct {
    kwork_t work;
    runtime_req_t runtime_req;      // Embedded
    platform_req_t platform_req;    // Embedded in runtime_req
    device_req_t device_req;        // Embedded in platform_req
} user_req_t;

user_req_t req;  // Single allocation, entire stack
```

No malloc/free during delegation = fast and predictable.

### Batch Processing in tick()

Process multiple completions in one tick:

```c
void runtime_tick(void* state) {
    runtime_t* rt = (runtime_t*)state;

    // Process all ready work
    kwork_t* work;
    while ((work = dequeue_ready(rt)) != NULL) {
        work->callback(work, work->err);
    }
}
```

Amortizes tick overhead across multiple completions.

### Store Work Pointers in Device State

When hardware completes operations, you need to find the corresponding work item. Store pointers:

```c
typedef struct {
    machine_t machine;

    // Map descriptor index to work item
    kwork_t* pending_work[QUEUE_SIZE];
} virtio_device_t;

void virtio_submit(void* state, kwork_t* work) {
    virtio_device_t* dev = (virtio_device_t*)state;

    uint16_t desc_idx = alloc_descriptor(dev);
    dev->pending_work[desc_idx] = work;  // Store pointer

    submit_to_hardware(dev, desc_idx);
}

void virtio_irq_handler(void* ctx) {
    virtio_device_t* dev = (virtio_device_t*)ctx;

    uint16_t desc_idx = get_completed_descriptor(dev);
    kwork_t* work = dev->pending_work[desc_idx];  // Retrieve pointer

    work->requester->complete(work->requester->state, work, KERR_OK);
}
```

O(1) lookup instead of searching a queue.

### Minimize State Transitions

Work state transitions have overhead. Minimize unnecessary transitions:

```c
// BAD: Many transitions
work->state = KWORK_STATE_SUBMIT;
// ... later
work->state = KWORK_STATE_LIVE;
// ... later
work->state = KWORK_STATE_PROCESSING;
// ... later
work->state = KWORK_STATE_READY;

// GOOD: Minimal transitions
work->state = KWORK_STATE_LIVE;
// ... later
work->state = KWORK_STATE_READY;
```

## 19. Tracing and Debugging

### Requester Chain Tracing

Walk the requester chain to trace work:

```c
void trace_work_chain(kwork_t* work) {
    printf("Work trace:\n");
    printf("  Work: op=%u state=%u\n", work->op, work->state);
    printf("  Requester: %p\n", work->requester);

    // If we store parent work pointer, can walk up:
    // parent_work = get_parent_work(work);
    // trace_work_chain(parent_work);
}
```

### Instrumentation

Add hooks at machine boundaries:

```c
void my_machine_submit(void* state, kwork_t* work) {
    TRACE_EVENT("my_machine_submit", "op", work->op);

    // ... normal logic
}

void my_machine_complete(void* state, kwork_t* work, kerr_t err) {
    TRACE_EVENT("my_machine_complete", "op", work->op, "err", err);

    // ... normal logic
}
```

Export to trace tools (Perfetto, Chrome Tracing, etc.).

### State Machine Invariants

Assert state machine invariants:

```c
void my_machine_submit(void* state, kwork_t* work) {
    // Work must be DEAD before submission
    assert(work->state == KWORK_STATE_DEAD);

    // ... process work

    work->state = KWORK_STATE_LIVE;
}

void my_machine_complete(void* state, kwork_t* work, kerr_t err) {
    // Work must be LIVE when completing
    assert(work->state == KWORK_STATE_LIVE);

    // ... process completion

    // Forward with state transition
    work->state = KWORK_STATE_READY;
    work->requester->complete(...);
}
```

Catch bugs early.

### Debug Builds

Add validation in debug builds:

```c
#ifdef DEBUG
#define VALIDATE_WORK(work) do { \
    assert(work != NULL); \
    assert(work->requester != NULL); \
    assert(work->callback != NULL); \
    assert(work->state < KWORK_STATE_MAX); \
} while (0)
#else
#define VALIDATE_WORK(work) do { } while (0)
#endif

void my_machine_submit(void* state, kwork_t* work) {
    VALIDATE_WORK(work);
    // ...
}
```

Zero overhead in release builds.

## 20. Common Pitfalls

### Pitfall 1: Over-Layering

**Problem**: Too many machine layers for simple operations.

```c
// BAD: Excessive layers
User → Runtime → Platform → Abstraction Layer → Device Wrapper → Device Driver

// GOOD: Appropriate layers
User → Runtime → Platform → Device Driver
```

**Solution**: Only add layers when they provide clear value (abstraction, reuse, testability).

### Pitfall 2: Forcing Async on Sync Operations

**Problem**: Using async `submit()` for inherently synchronous operations.

```c
// BAD: Async MMIO read
mmio_read_req_t req;
machine.submit(&machine, &req.work);
// Wait for callback...

// GOOD: Sync MMIO read
mmio_read_req_t req;
machine.query(&machine, &req.work);
uint32_t value = req.value;
```

**Solution**: Use `query()` for fast synchronous operations.

### Pitfall 3: Not Setting Requester

**Problem**: Forgetting to set `work->requester` when delegating.

```c
// BAD: Requester not set
req->child_req.work.op = CHILD_OP;
m->child->submit(m->child->state, &req->child_req.work);

// GOOD: Requester set
req->child_req.work.requester = &my_machine;
req->child_req.work.op = CHILD_OP;
m->child->submit(m->child->state, &req->child_req.work);
```

**Solution**: Always set requester before delegating.

### Pitfall 4: Incorrect CONTAINER_OF

**Problem**: Using wrong offset or type with `CONTAINER_OF`.

```c
// BAD: Wrong type
child_req_t* req = CONTAINER_OF(work, parent_req_t, work);  // Type mismatch!

// GOOD: Correct types
child_req_t* child_req = CONTAINER_OF(work, child_req_t, work);
parent_req_t* parent_req = CONTAINER_OF(child_req, parent_req_t, child_req);
```

**Solution**: Double-check types and field names.

### Pitfall 5: Not Handling Completion Semantics

**Problem**: Expecting completion to work differently than implemented.

```c
// BAD: Assuming immediate callback after complete()
machine->complete(machine->state, work, KERR_OK);
// Callback not invoked yet! Runtime might defer it.

// GOOD: Understanding completion is deferred
machine->complete(machine->state, work, KERR_OK);
// Callback will be invoked later in tick()
```

**Solution**: Understand that completion != callback invocation.

### Pitfall 6: Memory Lifetime Issues

**Problem**: Freeing request while it's still in flight.

```c
// BAD: Stack allocation for async work
void my_function() {
    my_req_t req;  // Stack allocation
    machine.submit(&machine, &req.work);
    // Function returns, req destroyed, but work still in flight!
}

// GOOD: Allocation with correct lifetime
my_req_t* req = allocate_request();  // Heap or pool
machine.submit(&machine, &req->work);
// req lives until callback invoked
```

**Solution**: Ensure request lives until callback completes.

### Pitfall 7: Not Propagating Cancellation

**Problem**: Not cancelling child work when parent is cancelled.

```c
// BAD: Cancel doesn't propagate
void my_machine_cancel(void* state, kwork_t* work) {
    // Mark work cancelled but don't cancel child
    work->state = KWORK_STATE_CANCELLED;
}

// GOOD: Cancel propagates
void my_machine_cancel(void* state, kwork_t* work) {
    my_machine_t* m = (my_machine_t*)state;
    my_req_t* req = CONTAINER_OF(work, my_req_t, work);

    // Cancel child work
    m->child->cancel(m->child->state, &req->child_req.work);
}
```

**Solution**: Propagate cancellation to child machines.

## 21. Conclusion

### Key Takeaways

1. **Uniform Interface**: All machines expose same methods, enabling composition and swapping
2. **Async Stack Pattern**: Embed child requests in parent requests for zero-allocation delegation
3. **Operation Code Scoping**: Per-machine opcodes enable independent operation spaces
4. **Explicit Completion**: Requester field + complete() method separates completion from callback
5. **Sync/Async Separation**: Use query() for fast synchronous operations, submit() for async
6. **Traceability**: Requester chain enables full system tracing

### The Strongest Version

The strongest version of the machine pattern combines:
- **Conceptual elegance**: One pattern for all layers
- **Performance**: Zero-allocation async, fast sync path
- **Flexibility**: Variable delegation depth, swappable machines
- **Simplicity**: Clear interfaces, explicit data flow

It achieves this by:
- Not forcing uniformity where it doesn't help (sync vs async)
- Using allocation strategically (async stack, not dynamic allocation)
- Making dependencies explicit (requester field, delegate pointers)
- Providing escape hatches (query() for sync, direct function calls where appropriate)

### Future Directions

**Operation Registries**: Dynamic opcode registration for runtime composition

**Work Pools**: Pre-allocated work item pools for bounded resource usage

**Formal Verification**: Prove state machine invariants hold

**Performance Optimization**:
- Lock-free work queues
- NUMA-aware machine placement
- Zero-copy delegation

**Richer Tracing**:
- Structured trace events (Perfetto format)
- Automatic trace generation
- Performance profiling integration

**Generic Programming**:
- Template-based machine implementations (C++ or Rust)
- Type-safe operation codes
- Compile-time verification

### Applying the Pattern

The machine pattern is valuable when:
- Building event-driven systems
- Dealing with multiple abstraction layers
- Need platform independence
- Performance matters (zero-allocation)
- Want composability and testability

Start simple:
1. Identify 2-3 natural layers
2. Define machine interface for each
3. Use async stack pattern for requests
4. Test each machine independently
5. Add layers as needed

The pattern scales from simple (2-3 machines) to complex (10+ layers) without losing clarity.

### Final Thought

The machine pattern is about **composable state machines** that communicate via **structured messages** (work items) with **explicit ownership** (requester field) and **zero allocation** (async stack pattern).

It's strongest when it balances **elegance with pragmatism**—using uniform interfaces where they help, and specialized APIs (query, direct calls) where they provide value.

Build systems that compose simply and perform efficiently.

---

*End of document.*

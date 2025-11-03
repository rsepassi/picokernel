---
**DESIGN DOCUMENT - UNIMPLEMENTED LANGUAGE**

This document describes a proposed programming language that does not currently exist. There is no compiler, no language implementation, and no code in VMOS uses this language. This is purely speculative future work.

For documentation of the current VMOS implementation, see the main documentation files in `doc/`.

---

# A Programming Language for the Machine Pattern

## Introduction

This document presents the design of a programming language that compiles to the machine pattern runtime described in [multimachine.md](multimachine.md). The language provides ergonomic async/await syntax while maintaining the **zero-allocation async stack property** through compile-time transformation.

### Design Goals

1. **Ergonomic syntax** - Natural async/await, select expressions, clean error handling
2. **Zero allocation** - Compile-time embedding of full async stack depth
3. **Type safety** - Effect types, machine interfaces, structured errors
4. **Performance** - Direct compilation to efficient machine pattern C code
5. **Composability** - First-class support for machine interfaces and swapping

### Key Innovation: Compile-Time Async Stack Embedding

The compiler analyzes async call graphs and generates request structures that embed all child requests for the full delegation depth. A single allocation by the caller contains the entire async stack:

```rust
// High-level code
async fn outer() {
    await inner();
}

// Compiler generates single structure containing full stack
typedef struct {
    kwork_t work;
    inner_req_t inner_req;  // Embedded (contains its children too)
} outer_req_t;

outer_req_t req;  // One allocation, entire async stack
```

This achieves **zero-allocation delegation** while maintaining high-level async/await ergonomics.

---

## Table of Contents

1. [Language Syntax](#language-syntax)
2. [Compilation Strategies](#compilation-strategies)
3. [Type System](#type-system)
4. [Concrete Compilation Examples](#concrete-compilation-examples)
5. [Comparisons to Existing Languages](#comparisons-to-existing-languages)
6. [Advanced Features](#advanced-features)
7. [Compiler Architecture](#compiler-architecture)
8. [Conclusion](#conclusion)

---

## Language Syntax

### Machine Definitions

Machines are first-class constructs with explicit state and operation contracts:

```rust
machine NetworkStack {
    // Machine state
    state {
        arp_cache: ArpCache,
        local_ip: IpAddr,
        local_mac: MacAddr,
        virtio_net: Machine<VirtioNet>,  // Delegate machine
    }

    // Async operations (compile to submit/complete)
    async fn udp_send(dest_ip: IpAddr, dest_port: u16, payload: &[u8]) -> Result<()> {
        // Build UDP/IP/Ethernet frame
        let frame = self.build_udp_frame(dest_ip, dest_port, payload);

        // Delegate to VirtIO (compiler embeds virtio_tx_req in this request)
        await self.virtio_net.tx(frame)
    }

    async fn udp_recv() -> Result<Packet> {
        // Standing receive
        let frame = await self.virtio_net.rx();
        self.parse_udp_frame(frame)
    }

    // Sync operations (compile to query method)
    query fn get_local_ip() -> IpAddr {
        self.local_ip
    }

    query fn arp_lookup(ip: IpAddr) -> Option<MacAddr> {
        self.arp_cache.lookup(ip)
    }

    // Pure helper functions
    fn build_udp_frame(&self, dest: IpAddr, port: u16, payload: &[u8]) -> Frame {
        // Synchronous computation
        // ...
    }
}
```

**Key features:**
- `machine` keyword declares a machine with state and operations
- `async fn` for asynchronous operations that return futures
- `query fn` for synchronous operations with immediate return
- Regular `fn` for pure helper functions
- Delegate machines typed as `Machine<T>` for swappability

### Async Operations

Standard async/await syntax:

```rust
async fn send_response(user: &User, dest: IpAddr) -> Result<()> {
    // Sequential async operations
    let data = await generate_response(user);
    await network_send(dest, data);
    await log_event("response_sent");
    Ok(())
}

// Using ? for error propagation
async fn fetch_and_process() -> Result<Data> {
    let raw = await fetch_data()?;
    let processed = process(raw)?;
    await store(processed)?;
    Ok(processed)
}
```

**Compilation:** Each `await` point becomes a state in the generated state machine, with embedded requests for all async calls.

### Sync Operations (Query)

Synchronous operations use the `query` keyword:

```rust
// Query operation - immediate return, no await
let time = query platform.get_time();
let value = query platform.mmio_read(addr);

// Query in control flow
fn check_device_ready() -> bool {
    let status = query device.get_status();  // Immediate return
    status.ready
}
```

**Compilation:** `query expr` compiles to `machine.query(req)` with immediate return, no state machine, no callback.

### Select Expressions

Race multiple async operations, completing when any finishes:

```rust
// Simple timeout pattern
async fn wait_with_timeout<T>(operation: Future<T>, timeout_ms: u64) -> Result<T> {
    select {
        result = await operation => Ok(result),
        _ = await timer(timeout_ms) => Err(Error::Timeout),
    }
}

// Multiple events
async fn handle_events() {
    loop {
        select {
            packet = await net_recv() => handle_packet(packet),
            block = await disk_read() => process_block(block),
            _ = await timer(100) => check_timeouts(),
        }
    }
}

// With error handling
async fn resilient_fetch() -> Result<Data> {
    select {
        Ok(data) = await primary_fetch() => Ok(data),
        Err(_) = await primary_fetch() => {
            // Fallback on error
            await secondary_fetch()
        },
        _ = await timer(5000) => Err(Error::Timeout),
    }
}
```

**Compilation:** All branches are submitted concurrently with embedded requests. First completion cancels remaining branches.

### Error Handling

Explicit `Result` types with `?` operator:

```rust
// Error enum
enum NetworkError {
    Timeout,
    Disconnected,
    InvalidPacket,
    QueueFull,
}

type Result<T> = std::result::Result<T, NetworkError>;

// Error propagation
async fn send_with_retry(data: &[u8]) -> Result<()> {
    match await network_send(data) {
        Ok(()) => Ok(()),
        Err(NetworkError::QueueFull) => {
            await timer(10);
            await network_send(data)  // Retry
        }
        Err(e) => Err(e),  // Propagate
    }
}

// Using ?
async fn complex_operation() -> Result<Data> {
    let step1 = await fetch_data()?;  // Early return on error
    let step2 = await process(step1)?;
    let step3 = await store(step2)?;
    Ok(step3)
}
```

**Compilation:** `Result<T>` maps to `kerr_t` error codes. `?` operator generates completion handlers that propagate errors up the requester chain.

---

## Compilation Strategies

### Machine Struct Generation

Machine definitions compile to C structures with the standard `machine_t` interface:

**Source:**
```rust
machine NetworkStack {
    state {
        arp_cache: ArpCache,
        virtio_net: Machine<VirtioNet>,
    }
    async fn udp_send(...) -> Result<()> { ... }
    query fn get_local_ip() -> IpAddr { ... }
}
```

**Compiled C:**
```c
typedef struct {
    machine_t machine;  // Standard interface

    // State fields
    arp_cache_t arp_cache;
    machine_t* virtio_net;
} netstack_t;

// Generated init function
void netstack_init(netstack_t* net, machine_t* virtio_net) {
    // Set up interface
    net->machine.state = net;
    net->machine.submit = netstack_submit;
    net->machine.complete = netstack_complete;
    net->machine.query = netstack_query;
    net->machine.tick = netstack_tick;
    net->machine.next_delay = netstack_next_delay;
    net->machine.wait = netstack_wait;
    net->machine.cancel = netstack_cancel;

    // Store delegate reference
    net->virtio_net = virtio_net;

    // Initialize state
    arp_cache_init(&net->arp_cache);
}
```

### Request Type Generation with Embedded Children

The compiler analyzes async call graphs to compute maximum delegation depth and generates request types with embedded child requests.

**Algorithm:**

1. **Build call graph** - Identify all async functions and their async calls
2. **Compute depth** - Calculate maximum delegation depth for each function
3. **Generate types bottom-up** - Start with leaf operations (no children), work up
4. **Embed children** - Each request embeds all possible child requests
5. **Conservative branching** - If control flow branches, embed requests for all branches

**Example:**

```rust
// Source
async fn outer() {
    await middle();
}

async fn middle() {
    await inner();
}

async fn inner() {
    await leaf_operation();
}
```

**Compiler generates (depth-first):**

```c
// Leaf request (no children)
typedef struct {
    kwork_t work;
    // leaf operation parameters
    uint32_t leaf_param;
} leaf_req_t;

// Inner request (embeds leaf)
typedef struct {
    kwork_t work;
    uint32_t inner_param;
    leaf_req_t leaf_req;  // EMBEDDED
} inner_req_t;

// Middle request (embeds inner, which embeds leaf)
typedef struct {
    kwork_t work;
    uint32_t middle_param;
    inner_req_t inner_req;  // EMBEDDED (contains leaf)
} middle_req_t;

// Outer request (full stack)
typedef struct {
    kwork_t work;
    uint32_t outer_param;
    middle_req_t middle_req;  // EMBEDDED (full tree)
} outer_req_t;

// User allocates outer_req_t once - contains ENTIRE async stack:
// outer_req_t {
//   work,
//   outer_param,
//   middle_req {
//     work,
//     middle_param,
//     inner_req {
//       work,
//       inner_param,
//       leaf_req {
//         work,
//         leaf_param
//       }
//     }
//   }
// }
```

### Submit/Complete Method Generation

Async functions compile to `submit` and `complete` methods:

**Source:**
```rust
async fn send_data(dest: IpAddr, data: &[u8]) -> Result<()> {
    let packet = build_packet(dest, data);
    await network.send(packet)
}
```

**Compiled C:**

```c
// Request type
typedef struct {
    kwork_t work;
    ip_addr_t dest;
    uint8_t* data;
    size_t data_len;
    // Embedded child request
    network_send_req_t network_req;
} send_data_req_t;

// Submit implementation
void send_data_submit(void* state, kwork_t* work) {
    machine_t* m = (machine_t*)state;
    send_data_req_t* req = CONTAINER_OF(work, send_data_req_t, work);

    // Synchronous part: build packet
    uint8_t packet[1500];
    size_t packet_len = build_packet(packet, req->dest, req->data, req->data_len);

    // Initialize embedded child request
    req->network_req.work.requester = m;
    req->network_req.work.callback = send_data_complete;
    req->network_req.work.op = NETWORK_OP_SEND;
    req->network_req.work.state = KWORK_STATE_DEAD;
    req->network_req.packet = packet;
    req->network_req.packet_len = packet_len;

    // Submit to child machine
    m->network->submit(m->network->state, &req->network_req.work);

    work->state = KWORK_STATE_LIVE;
}

// Complete implementation
void send_data_complete(void* state, kwork_t* work, kerr_t err) {
    // work is &req->network_req.work
    network_send_req_t* network_req = CONTAINER_OF(work, network_send_req_t, work);
    send_data_req_t* req = CONTAINER_OF(network_req, send_data_req_t, network_req);

    // Forward completion to parent
    req->work.requester->complete(
        req->work.requester->state,
        &req->work,
        err
    );
}
```

### Multi-Step State Machine

For async functions with multiple `await` points, the compiler generates a state machine:

**Source:**
```rust
async fn multi_step() -> Result<Data> {
    let step1 = await operation1();
    let step2 = await operation2(step1);
    let step3 = await operation3(step2);
    Ok(step3)
}
```

**Compiled C:**

```c
typedef struct {
    kwork_t work;

    // Embedded requests for each step
    op1_req_t op1_req;
    op2_req_t op2_req;
    op3_req_t op3_req;

    // State machine tracking
    uint8_t current_step;

    // Intermediate results
    data_t step1_result;
    data_t step2_result;
} multi_step_req_t;

// Submit - start step 1
void multi_step_submit(void* state, kwork_t* work) {
    machine_t* m = (machine_t*)state;
    multi_step_req_t* req = CONTAINER_OF(work, multi_step_req_t, work);

    req->current_step = 1;

    // Submit operation 1
    req->op1_req.work.requester = m;
    req->op1_req.work.callback = multi_step_op1_complete;
    // ... initialize ...
    submit_child(&req->op1_req.work);
}

// Step 1 complete - proceed to step 2
void multi_step_op1_complete(void* state, kwork_t* work, kerr_t err) {
    op1_req_t* op1_req = CONTAINER_OF(work, op1_req_t, work);
    multi_step_req_t* req = CONTAINER_OF(op1_req, multi_step_req_t, op1_req);

    if (err != KERR_OK) {
        // Propagate error
        req->work.requester->complete(req->work.requester->state, &req->work, err);
        return;
    }

    // Save result
    req->step1_result = op1_req->result;
    req->current_step = 2;

    // Submit operation 2
    req->op2_req.work.requester = m;
    req->op2_req.work.callback = multi_step_op2_complete;
    req->op2_req.input = req->step1_result;
    submit_child(&req->op2_req.work);
}

// Step 2 complete - proceed to step 3
void multi_step_op2_complete(void* state, kwork_t* work, kerr_t err) {
    op2_req_t* op2_req = CONTAINER_OF(work, op2_req_t, work);
    multi_step_req_t* req = CONTAINER_OF(op2_req, multi_step_req_t, op2_req);

    if (err != KERR_OK) {
        req->work.requester->complete(req->work.requester->state, &req->work, err);
        return;
    }

    req->step2_result = op2_req->result;
    req->current_step = 3;

    // Submit operation 3
    req->op3_req.work.requester = m;
    req->op3_req.work.callback = multi_step_op3_complete;
    req->op3_req.input = req->step2_result;
    submit_child(&req->op3_req.work);
}

// Step 3 complete - done
void multi_step_op3_complete(void* state, kwork_t* work, kerr_t err) {
    op3_req_t* op3_req = CONTAINER_OF(work, op3_req_t, work);
    multi_step_req_t* req = CONTAINER_OF(op3_req, multi_step_req_t, op3_req);

    // Forward final result
    req->work.requester->complete(req->work.requester->state, &req->work, err);
}
```

### Requester Chain Setup

The compiler ensures the requester chain is correctly established during submission:

```c
// Parent submits to machine A
parent_req.work.requester = &parent_machine;
machine_a.submit(machine_a.state, &parent_req.work);

// Machine A's submit sets up child request
void machine_a_submit(void* state, kwork_t* work) {
    // Extract request
    machine_a_req_t* req = CONTAINER_OF(work, machine_a_req_t, work);

    // Set requester for child (machine A is requester)
    req->child_req.work.requester = &machine_a;

    // Submit to child
    machine_b.submit(machine_b.state, &req->child_req.work);
}

// Requester chain forms automatically:
// parent_machine <- machine_a <- machine_b
```

**Verification:** Compiler can optionally insert assertions in debug builds to verify requester is set before submission.

---

## Type System

### Effect Types

Operations are effect-typed to distinguish async, sync, and pure operations:

```rust
// Async effect - returns Future<T>
async fn network_send(data: &[u8]) -> Result<()> { ... }

// Sync effect - returns T immediately
query fn get_status() -> Status { ... }

// Pure function - no effects, no side effects
fn compute_checksum(data: &[u8]) -> u32 { ... }

// Effect polymorphism
fn process<F: AsyncFn<T>>(op: F) -> Future<T> {
    await op()
}
```

**Effect checking rules:**
- `async fn` can only be called with `await`
- `query fn` returns immediately, cannot be awaited
- Cannot `await` inside `query fn` (compile error)
- Cannot call `async fn` inside `query fn` (compile error)
- Pure functions can be called anywhere

### Machine Capabilities/Interfaces

Machines declare capability interfaces for type-safe swapping:

```rust
// Interface definition
interface Storage {
    async fn read(sector: u64, buffer: &mut [u8]) -> Result<usize>;
    async fn write(sector: u64, data: &[u8]) -> Result<()>;
    async fn flush() -> Result<()>;
    query fn get_capacity() -> u64;
}

// Multiple implementations
machine VirtioBlock implements Storage {
    state { /* VirtIO-specific state */ }

    async fn read(sector: u64, buffer: &mut [u8]) -> Result<usize> {
        // VirtIO implementation
    }
    // ... other methods
}

machine NvmeDevice implements Storage {
    state { /* NVMe-specific state */ }

    async fn read(sector: u64, buffer: &mut [u8]) -> Result<usize> {
        // NVMe implementation
    }
    // ... other methods
}

machine RamDisk implements Storage {
    state { ram: Vec<u8> }

    async fn read(sector: u64, buffer: &mut [u8]) -> Result<usize> {
        // RAM disk implementation (actually synchronous)
        buffer.copy_from_slice(&self.ram[sector * 512..]);
        Ok(buffer.len())
    }
    // ... other methods
}

// Generic over capability interface
machine FileSystem<S: Storage> {
    state {
        storage: Machine<S>,
        metadata: MetadataCache,
    }

    async fn read_file(path: &str) -> Result<Vec<u8>> {
        // Can call any Storage operation
        let sectors = self.metadata.lookup(path)?;
        let mut data = Vec::new();
        for sector in sectors {
            await self.storage.read(sector, &mut buffer)?;
            data.extend_from_slice(&buffer);
        }
        Ok(data)
    }
}

// Instantiation with specific implementation
let fs = FileSystem::<VirtioBlock>::new(virtio_machine);

// Swap implementation at init time
let fs = FileSystem::<RamDisk>::new(ram_disk_machine);
```

**Benefits:**
- Type-safe machine swapping
- Compile-time verification of operation contracts
- Generic code over machine interfaces
- Clear capability boundaries

### Request/Response Types

Request and response types are explicit:

```rust
// Request type
struct SendRequest {
    dest: IpAddr,
    dest_port: u16,
    data: Vec<u8>,
}

// Response type
struct SendResponse {
    bytes_sent: usize,
}

// Operation signature
async fn send(req: SendRequest) -> Result<SendResponse> {
    // ... implementation
}

// Usage
let req = SendRequest {
    dest: ip_addr("192.168.1.1"),
    dest_port: 80,
    data: vec![0, 1, 2, 3],
};
let resp = await send(req)?;
println!("Sent {} bytes", resp.bytes_sent);
```

**Compilation:** Compiler generates C structure containing both request and response fields:

```c
typedef struct {
    kwork_t work;
    // Request fields
    ip_addr_t dest;
    uint16_t dest_port;
    uint8_t* data;
    size_t data_len;
    // Response fields (filled on completion)
    size_t bytes_sent;
    // Embedded children
    child_req_t child_req;
} send_request_t;
```

### Structured Error Types

Error types are enums that map to error codes:

```rust
// Error enum
enum NetworkError {
    Timeout,
    Disconnected,
    InvalidPacket,
    QueueFull,
    HostUnreachable,
}

// Compiler generates error code mapping
// #define NETWORK_ERR_TIMEOUT         0x1001
// #define NETWORK_ERR_DISCONNECTED    0x1002
// #define NETWORK_ERR_INVALID_PACKET  0x1003
// #define NETWORK_ERR_QUEUE_FULL      0x1004
// #define NETWORK_ERR_HOST_UNREACHABLE 0x1005

// Result type
type Result<T> = std::result::Result<T, NetworkError>;

// Error handling
async fn robust_send(data: &[u8]) -> Result<()> {
    match await network_send(data) {
        Ok(()) => Ok(()),
        Err(NetworkError::QueueFull) => {
            await timer(10);
            await network_send(data)  // Retry
        }
        Err(NetworkError::Timeout) => {
            // Try alternate route
            await send_via_fallback(data)
        }
        Err(e) => Err(e),  // Propagate other errors
    }
}
```

---

## Concrete Compilation Examples

### Example 1: Simple Async Function

**High-level source:**

```rust
async fn fetch_random_bytes(count: usize) -> Result<Vec<u8>> {
    let mut buffer = vec![0u8; count];
    await rng_read(&mut buffer)?;
    Ok(buffer)
}
```

**Compiled C code:**

```c
// Request structure
typedef struct {
    kwork_t work;

    // Parameters
    uint8_t* buffer;
    size_t count;

    // Embedded child request
    krng_req_t rng_req;
} fetch_random_bytes_req_t;

// Submit implementation
void fetch_random_bytes_submit(void* state, kwork_t* work) {
    platform_t* p = (platform_t*)state;
    fetch_random_bytes_req_t* req = CONTAINER_OF(work, fetch_random_bytes_req_t, work);

    // Initialize embedded RNG request
    req->rng_req.work.requester = &platform_machine;
    req->rng_req.work.callback = fetch_random_bytes_rng_complete;
    req->rng_req.work.op = KWORK_OP_RNG_READ;
    req->rng_req.work.state = KWORK_STATE_DEAD;
    req->rng_req.buffer = req->buffer;
    req->rng_req.length = req->count;

    // Submit to RNG device
    p->rng->submit(p->rng->state, &req->rng_req.work);

    work->state = KWORK_STATE_LIVE;
}

// Complete implementation
void fetch_random_bytes_rng_complete(void* state, kwork_t* work, kerr_t err) {
    krng_req_t* rng_req = CONTAINER_OF(work, krng_req_t, work);
    fetch_random_bytes_req_t* req = CONTAINER_OF(rng_req, fetch_random_bytes_req_t, rng_req);

    // Forward completion to parent
    req->work.requester->complete(
        req->work.requester->state,
        &req->work,
        err
    );
}
```

### Example 2: Select Expression

**High-level source:**

```rust
async fn wait_for_event() -> Event {
    select {
        packet = await net_recv() => Event::Packet(packet),
        block = await disk_read() => Event::Block(block),
        _ = await timer(1000) => Event::Timeout,
    }
}
```

**Compiled C code:**

```c
// Event enum
typedef enum {
    EVENT_TYPE_PACKET,
    EVENT_TYPE_BLOCK,
    EVENT_TYPE_TIMEOUT,
} event_type_t;

// Request with ALL branches embedded
typedef struct {
    kwork_t work;

    // Which branch completed first
    event_type_t completed_branch;
    bool has_completed;

    // Embedded requests for ALL branches
    knet_recv_req_t net_req;
    kblk_req_t disk_req;
    ktimer_req_t timer_req;

    // Result storage
    union {
        knet_packet_t packet;
        kblk_segment_t block;
    } result;
} wait_for_event_req_t;

// Submit - submits ALL branches concurrently
void wait_for_event_submit(void* state, kwork_t* work) {
    platform_t* p = (platform_t*)state;
    wait_for_event_req_t* req = CONTAINER_OF(work, wait_for_event_req_t, work);

    req->has_completed = false;

    // Submit network receive
    req->net_req.work.requester = &platform_machine;
    req->net_req.work.callback = wait_for_event_net_complete;
    req->net_req.work.op = KWORK_OP_NET_RECV;
    req->net_req.work.state = KWORK_STATE_DEAD;
    p->network->submit(p->network->state, &req->net_req.work);

    // Submit disk read
    req->disk_req.work.requester = &platform_machine;
    req->disk_req.work.callback = wait_for_event_disk_complete;
    req->disk_req.work.op = KWORK_OP_BLOCK_READ;
    req->disk_req.work.state = KWORK_STATE_DEAD;
    p->storage->submit(p->storage->state, &req->disk_req.work);

    // Submit timer
    req->timer_req.work.requester = &platform_machine;
    req->timer_req.work.callback = wait_for_event_timer_complete;
    req->timer_req.work.op = KWORK_OP_TIMER;
    req->timer_req.work.state = KWORK_STATE_DEAD;
    req->timer_req.timeout_ns = 1000000000;  // 1 second
    ksubmit(kernel, &req->timer_req.work);

    work->state = KWORK_STATE_LIVE;
}

// Network completion - cancel others if first
void wait_for_event_net_complete(void* state, kwork_t* work, kerr_t err) {
    knet_recv_req_t* net_req = CONTAINER_OF(work, knet_recv_req_t, work);
    wait_for_event_req_t* req = CONTAINER_OF(net_req, wait_for_event_req_t, net_req);

    // Check if we're first to complete
    if (!req->has_completed) {
        req->has_completed = true;
        req->completed_branch = EVENT_TYPE_PACKET;

        // Cancel other branches
        platform_t* p = (platform_t*)state;
        p->storage->cancel(p->storage->state, &req->disk_req.work);
        kcancel(kernel, &req->timer_req.work);

        // Store result
        req->result.packet = net_req->packet;

        // Complete to parent
        req->work.requester->complete(
            req->work.requester->state,
            &req->work,
            err
        );
    }
    // Else: another branch completed first, ignore this completion
}

// Disk completion - similar logic
void wait_for_event_disk_complete(void* state, kwork_t* work, kerr_t err) {
    kblk_req_t* disk_req = CONTAINER_OF(work, kblk_req_t, work);
    wait_for_event_req_t* req = CONTAINER_OF(disk_req, wait_for_event_req_t, disk_req);

    if (!req->has_completed) {
        req->has_completed = true;
        req->completed_branch = EVENT_TYPE_BLOCK;

        // Cancel others
        platform_t* p = (platform_t*)state;
        p->network->cancel(p->network->state, &req->net_req.work);
        kcancel(kernel, &req->timer_req.work);

        req->result.block = disk_req->segment;

        req->work.requester->complete(
            req->work.requester->state,
            &req->work,
            err
        );
    }
}

// Timer completion - similar logic
void wait_for_event_timer_complete(void* state, kwork_t* work, kerr_t err) {
    ktimer_req_t* timer_req = CONTAINER_OF(work, ktimer_req_t, work);
    wait_for_event_req_t* req = CONTAINER_OF(timer_req, wait_for_event_req_t, timer_req);

    if (!req->has_completed) {
        req->has_completed = true;
        req->completed_branch = EVENT_TYPE_TIMEOUT;

        // Cancel others
        platform_t* p = (platform_t*)state;
        p->network->cancel(p->network->state, &req->net_req.work);
        p->storage->cancel(p->storage->state, &req->disk_req.work);

        req->work.requester->complete(
            req->work.requester->state,
            &req->work,
            KERR_TIMEOUT
        );
    }
}
```

**Key points:**
- All three branches embedded in single request structure
- All branches submitted concurrently
- First completion cancels others
- Zero allocation during operation

### Example 3: Deep Async Stack

**High-level source:**

```rust
async fn deep_stack() {
    await level1();
}

async fn level1() {
    await level2();
}

async fn level2() {
    await level3();
}

async fn level3() {
    await leaf_operation();
}
```

**Compiled C structures:**

```c
// Leaf (no children)
typedef struct {
    kwork_t work;
    uint32_t leaf_param;
} leaf_req_t;

// Level 3 embeds leaf
typedef struct {
    kwork_t work;
    uint32_t level3_param;
    leaf_req_t leaf_req;  // EMBEDDED
} level3_req_t;

// Level 2 embeds level3 (which contains leaf)
typedef struct {
    kwork_t work;
    uint32_t level2_param;
    level3_req_t level3_req;  // EMBEDDED (contains leaf_req)
} level2_req_t;

// Level 1 embeds level2 (which contains level3 and leaf)
typedef struct {
    kwork_t work;
    uint32_t level1_param;
    level2_req_t level2_req;  // EMBEDDED (contains level3_req and leaf_req)
} level1_req_t;

// Deep stack embeds full tree
typedef struct {
    kwork_t work;
    uint32_t deep_stack_param;
    level1_req_t level1_req;  // EMBEDDED (contains full tree)
} deep_stack_req_t;

// Memory layout of deep_stack_req_t:
// deep_stack_req_t {
//   work                   // 16 bytes (kwork_t)
//   deep_stack_param       // 4 bytes
//   level1_req {
//     work                 // 16 bytes
//     level1_param         // 4 bytes
//     level2_req {
//       work               // 16 bytes
//       level2_param       // 4 bytes
//       level3_req {
//         work             // 16 bytes
//         level3_param     // 4 bytes
//         leaf_req {
//           work           // 16 bytes
//           leaf_param     // 4 bytes
//         }
//       }
//     }
//   }
// }
// Total: ~100 bytes for entire async stack

// Single allocation by caller
deep_stack_req_t req;  // Contains ENTIRE delegation chain
```

**Submission cascade:**

```c
// deep_stack submit
void deep_stack_submit(void* state, kwork_t* work) {
    deep_stack_req_t* req = CONTAINER_OF(work, deep_stack_req_t, work);

    // Initialize level1_req
    req->level1_req.work.requester = &deep_stack_machine;
    req->level1_req.work.callback = deep_stack_level1_complete;

    // Submit to level1
    level1_machine.submit(&level1_machine, &req->level1_req.work);
}

// level1 submit
void level1_submit(void* state, kwork_t* work) {
    level1_req_t* req = CONTAINER_OF(work, level1_req_t, work);

    // Initialize level2_req (already embedded)
    req->level2_req.work.requester = &level1_machine;
    req->level2_req.work.callback = level1_level2_complete;

    // Submit to level2
    level2_machine.submit(&level2_machine, &req->level2_req.work);
}

// level2 submit
void level2_submit(void* state, kwork_t* work) {
    level2_req_t* req = CONTAINER_OF(work, level2_req_t, work);

    // Initialize level3_req (already embedded)
    req->level3_req.work.requester = &level2_machine;
    req->level3_req.work.callback = level2_level3_complete;

    // Submit to level3
    level3_machine.submit(&level3_machine, &req->level3_req.work);
}

// level3 submit
void level3_submit(void* state, kwork_t* work) {
    level3_req_t* req = CONTAINER_OF(work, level3_req_t, work);

    // Initialize leaf_req (already embedded)
    req->leaf_req.work.requester = &level3_machine;
    req->leaf_req.work.callback = level3_leaf_complete;

    // Submit to leaf
    leaf_machine.submit(&leaf_machine, &req->leaf_req.work);
}
```

**Completion propagation (walks back up):**

```c
// leaf completes → level3
void level3_leaf_complete(void* state, kwork_t* work, kerr_t err) {
    leaf_req_t* leaf_req = CONTAINER_OF(work, leaf_req_t, work);
    level3_req_t* req = CONTAINER_OF(leaf_req, level3_req_t, leaf_req);

    // Forward to level2
    req->work.requester->complete(req->work.requester->state, &req->work, err);
}

// level3 completes → level2
void level2_level3_complete(void* state, kwork_t* work, kerr_t err) {
    level3_req_t* level3_req = CONTAINER_OF(work, level3_req_t, work);
    level2_req_t* req = CONTAINER_OF(level3_req, level2_req_t, level3_req);

    // Forward to level1
    req->work.requester->complete(req->work.requester->state, &req->work, err);
}

// level2 completes → level1
void level1_level2_complete(void* state, kwork_t* work, kerr_t err) {
    level2_req_t* level2_req = CONTAINER_OF(work, level2_req_t, work);
    level1_req_t* req = CONTAINER_OF(level2_req, level1_req_t, level2_req);

    // Forward to deep_stack
    req->work.requester->complete(req->work.requester->state, &req->work, err);
}

// level1 completes → deep_stack
void deep_stack_level1_complete(void* state, kwork_t* work, kerr_t err) {
    level1_req_t* level1_req = CONTAINER_OF(work, level1_req_t, work);
    deep_stack_req_t* req = CONTAINER_OF(level1_req, deep_stack_req_t, level1_req);

    // Final completion
    req->work.requester->complete(req->work.requester->state, &req->work, err);
}
```

### Example 4: Error Propagation

**High-level source:**

```rust
async fn complex_operation() -> Result<Data> {
    let step1 = await fetch_data()?;   // Early return on error
    let step2 = await process(step1)?;
    let step3 = await store(step2)?;
    Ok(step3)
}
```

**Compiled C code:**

```c
typedef struct {
    kwork_t work;

    // Embedded requests
    fetch_data_req_t fetch_req;
    process_req_t process_req;
    store_req_t store_req;

    // State machine
    uint8_t current_step;

    // Intermediate results
    data_t step1_result;
    data_t step2_result;
} complex_operation_req_t;

// Submit step 1
void complex_operation_submit(void* state, kwork_t* work) {
    machine_t* m = (machine_t*)state;
    complex_operation_req_t* req = CONTAINER_OF(work, complex_operation_req_t, work);

    req->current_step = 1;

    // Submit fetch
    req->fetch_req.work.requester = m;
    req->fetch_req.work.callback = complex_operation_fetch_complete;
    // ... initialize ...
    submit_child(&req->fetch_req.work);
}

// Fetch complete - check error or proceed
void complex_operation_fetch_complete(void* state, kwork_t* work, kerr_t err) {
    fetch_data_req_t* fetch_req = CONTAINER_OF(work, fetch_data_req_t, work);
    complex_operation_req_t* req = CONTAINER_OF(fetch_req, complex_operation_req_t, fetch_req);

    if (err != KERR_OK) {
        // Error: propagate immediately (? operator)
        req->work.requester->complete(
            req->work.requester->state,
            &req->work,
            err  // Forward error code
        );
        return;
    }

    // Success: save result and proceed to step 2
    req->step1_result = fetch_req->result;
    req->current_step = 2;

    // Submit process
    req->process_req.work.requester = m;
    req->process_req.work.callback = complex_operation_process_complete;
    req->process_req.input = req->step1_result;
    submit_child(&req->process_req.work);
}

// Process complete - check error or proceed
void complex_operation_process_complete(void* state, kwork_t* work, kerr_t err) {
    process_req_t* process_req = CONTAINER_OF(work, process_req_t, work);
    complex_operation_req_t* req = CONTAINER_OF(process_req, complex_operation_req_t, process_req);

    if (err != KERR_OK) {
        // Error: propagate (? operator)
        req->work.requester->complete(
            req->work.requester->state,
            &req->work,
            err
        );
        return;
    }

    // Success: proceed to step 3
    req->step2_result = process_req->result;
    req->current_step = 3;

    // Submit store
    req->store_req.work.requester = m;
    req->store_req.work.callback = complex_operation_store_complete;
    req->store_req.data = req->step2_result;
    submit_child(&req->store_req.work);
}

// Store complete - final step
void complex_operation_store_complete(void* state, kwork_t* work, kerr_t err) {
    store_req_t* store_req = CONTAINER_OF(work, store_req_t, work);
    complex_operation_req_t* req = CONTAINER_OF(store_req, complex_operation_req_t, store_req);

    // Forward completion (success or error)
    req->work.requester->complete(
        req->work.requester->state,
        &req->work,
        err
    );
}
```

**Key points:**
- Each `?` generates error check in completion handler
- Early return on error propagates up immediately
- No need to continue state machine on error
- Error code flows up requester chain

---

## Comparisons to Existing Languages

### vs Rust async/await

**Similarities:**
- Same syntax: `async fn`, `await`, `Future<T>`, `Result<T, E>`
- Same mental model: async functions return futures
- Same composition: futures compose naturally
- Similar error handling with `?` operator

**Differences:**

| Feature | Rust | This Language |
|---------|------|---------------|
| **Allocation model** | Poll-based, may allocate futures | Zero-allocation via compile-time async stack embedding |
| **Compilation** | State machine per future (implicit) | Explicit request structures with embedded children |
| **Runtime** | Executor polls futures | Event loop with submit/complete pattern |
| **Cancellation** | Drop future (implicit) | Explicit `kcancel()` call |
| **Select** | `tokio::select!` macro (library) | Built-in `select` expression (language) |
| **Sync ops** | Just call function | Explicit `query fn` with effect type |
| **Zero-copy** | Not guaranteed | Guaranteed by async stack pattern |

**Example comparison:**

```rust
// Rust
async fn fetch_data() -> Result<Data> {
    let response = reqwest::get(url).await?;
    let data = response.json().await?;
    Ok(data)
}
// Allocates future on heap (typically)
// Each await point may allocate

// This language
async fn fetch_data() -> Result<Data> {
    let response = await http_get(url)?;
    let data = await parse_json(response)?;
    Ok(data)
}
// Zero allocation - full stack embedded in single structure
// Allocated once by caller
```

### vs Go goroutines and channels

**Go approach:**
- M:N threading (goroutines scheduled on OS threads)
- Goroutines have growable stacks (start at 2KB)
- Channels for communication (buffered or unbuffered)
- Built-in `select` for channel operations

**This language approach:**
- Event loop with cooperative multitasking
- No separate stacks (single stack for synchronous parts)
- Direct machine delegation (no channels unless explicitly implemented)
- Built-in `select` for async operations

**Example comparison:**

```go
// Go
func fetchData() (Data, error) {
    respChan := make(chan Response)
    errChan := make(chan error)

    go func() {
        resp, err := httpGet(url)
        if err != nil {
            errChan <- err
            return
        }
        respChan <- resp
    }()

    select {
    case resp := <-respChan:
        return parseJson(resp)
    case err := <-errChan:
        return nil, err
    case <-time.After(timeout):
        return nil, ErrTimeout
    }
}
// Goroutine allocates stack
// Channels allocate buffers
```

```rust
// This language
async fn fetch_data() -> Result<Data> {
    select {
        resp = await http_get(url) => parse_json(resp),
        _ = await timer(timeout) => Err(Error::Timeout),
    }
}
// Zero allocation for select
// All branches embedded in single request
```

**Key differences:**
- Go uses preemptive scheduling; this uses cooperative
- Go goroutines have stacks; this uses async stack in request structures
- Go select allocates channels; this select embeds requests
- Go is runtime-heavy; this is compile-time transformation

### vs JavaScript/Python/C# async/await

Languages like JavaScript (V8), Python (asyncio), and C# compile async functions to state machines.

**Similarities:**
- Async/await syntax
- Compile to state machines
- Event loop execution model
- Promise/Future abstraction

**Differences:**

| Aspect | JS/Python/C# | This Language |
|--------|--------------|---------------|
| **State machine** | Yes (implicit) | Yes (explicit C code) |
| **Allocation** | Allocates promises/tasks | Zero allocation (async stack) |
| **Runtime** | Event loop + GC | Event loop, no GC |
| **Type safety** | Dynamic (JS/Python) or static (C#) | Static with effect types |
| **Interop** | Dynamic dispatch | Machine interface (vtable-like) |
| **Cancellation** | Cancellation tokens (library) | Built-in `kcancel()` |
| **Select** | `Promise.race()` etc. (library) | Built-in `select` expression |
| **Target** | Managed runtime | Bare metal / kernel |

**Example comparison:**

```javascript
// JavaScript
async function fetchData() {
    const response = await fetch(url);
    const data = await response.json();
    return data;
}
// Allocates promise objects
// GC collects when done
```

```rust
// This language
async fn fetch_data() -> Result<Data> {
    let response = await fetch(url)?;
    let data = await parse_json(response)?;
    Ok(data)
}
// No allocation during execution
// No GC needed
```

**Key innovation:** This language achieves the ergonomics of high-level async languages (JS/Python/C#) with the performance of low-level systems languages (C/Rust) by leveraging **compile-time embedding** of the full async stack.

### Comparison Summary Table

| Feature | Rust | Go | JS/Python | This Language |
|---------|------|----|-----------|----|
| **Syntax** | async/await | goroutines | async/await | async/await |
| **Allocation** | May allocate | Stack per goroutine | Allocates promises | Zero (async stack) |
| **Concurrency** | Cooperative | Preemptive M:N | Cooperative | Cooperative |
| **Select** | tokio::select! | Built-in | Promise.race() | Built-in |
| **Type safety** | Strong | Strong | Weak (JS/Py) | Strong + effects |
| **Runtime** | Executor | Scheduler | Event loop + GC | Event loop |
| **Target** | Systems | Application | Application | Kernel/bare-metal |
| **Zero-copy** | Not guaranteed | No | No | Yes (guaranteed) |

---

## Advanced Features

### Generic Async Functions

The language supports generic async functions with effect polymorphism:

```rust
// Generic retry combinator
async fn retry<T, F: AsyncFn() -> Result<T>>(
    op: F,
    max_attempts: u32,
    backoff_ms: u64
) -> Result<T> {
    for attempt in 0..max_attempts {
        match await op() {
            Ok(result) => return Ok(result),
            Err(e) if attempt < max_attempts - 1 => {
                await timer(backoff_ms * (1 << attempt));  // Exponential backoff
                continue;
            }
            Err(e) => return Err(e),
        }
    }
}

// Usage
let data = await retry(
    || fetch_data(url),
    max_attempts: 3,
    backoff_ms: 100
)?;
```

**Compilation:** Monomorphization generates specialized versions for each concrete type, similar to Rust generics.

### Standing Work Items

Standing work items automatically resubmit after completion, useful for continuous operations like packet receiving:

```rust
// Standing receive - callback fires repeatedly
standing async fn packet_receiver() -> Packet {
    await net_recv()  // Auto-resubmits after completion
}

// Spawn standing work
standing_spawn!(packet_receiver(), |packet| {
    handle_packet(packet);
    // Work automatically resubmitted
});
```

**Compilation:**
- Sets `KWORK_FLAG_STANDING` flag in work item
- Runtime keeps work LIVE after completion instead of transitioning to DEAD
- Callback invoked, then work immediately resubmitted

**Use cases:**
- Network packet reception
- Interrupt handling
- Event monitoring
- Continuous sensor reading

### Machine Composition and Layering

Machines can be composed vertically to build layered systems:

```rust
// Lower-level machine
machine VirtioNet {
    state { /* hardware state */ }

    async fn tx(frame: &[u8]) -> Result<()> { /* ... */ }
    async fn rx() -> Result<Frame> { /* ... */ }
}

// Mid-level machine (uses VirtioNet)
machine NetworkStack<Device: NetworkDevice> {
    state {
        device: Machine<Device>,
        arp_cache: ArpCache,
    }

    async fn udp_send(dest_ip: IpAddr, dest_port: u16, data: &[u8]) -> Result<()> {
        let frame = self.build_udp_frame(dest_ip, dest_port, data);
        await self.device.tx(frame)
    }
}

// High-level machine (uses NetworkStack)
machine HttpClient<Net: NetworkStack> {
    state {
        network: Machine<Net>,
    }

    async fn get(url: &str) -> Result<Response> {
        let request = self.build_http_request(url);
        await self.network.udp_send(host, 80, request.as_bytes())
    }
}

// Instantiate entire stack
let virtio = VirtioNet::new(pci_base);
let netstack = NetworkStack::<VirtioNet>::new(virtio);
let http = HttpClient::<NetworkStack>::new(netstack);

// Or swap lower layers
let ram_device = RamNetDevice::new();
let netstack = NetworkStack::<RamNetDevice>::new(ram_device);
let http = HttpClient::<NetworkStack>::new(netstack);  // Same HTTP code
```

**Benefits:**
- Clear separation of concerns
- Swappable implementations at each layer
- Testability (mock lower layers)
- Reusability across platforms

---

## Compiler Architecture

### Compilation Phases

1. **Parsing** - Source code → AST (Abstract Syntax Tree)
2. **Name resolution** - Resolve identifiers, imports, machine references
3. **Type checking** - Verify types, check effect constraints
4. **Async analysis** - Build async call graph, compute max depths
5. **Monomorphization** - Generate specialized code for generic functions
6. **Code generation** - Emit C structures and functions
7. **Linking** - Link with runtime library (kernel, platform)

### Async Analysis Phase

The compiler performs sophisticated analysis to compute async stack depths:

**Algorithm:**

```
1. Build call graph of all async functions
2. Identify cycles (recursive async calls - error or bounded depth)
3. Compute maximum depth for each function:
   depth(f) = max(depth(g) for all async calls g in f) + 1
4. For each async function:
   - Identify all async calls in function body
   - For branches/conditionals, take union of all branches
   - For loops, compute worst-case iteration depth
5. Generate request types bottom-up:
   - Leaf functions (no async calls): simple request
   - Non-leaf functions: embed all child requests
```

**Example:**

```rust
async fn a() { await b(); }
async fn b() { await c(); await d(); }
async fn c() { await leaf(); }
async fn d() { await leaf(); }
async fn leaf() { /* no async calls */ }

// Depths:
// leaf: 0
// c: 1 (calls leaf)
// d: 1 (calls leaf)
// b: 2 (calls c and d, max depth is 1, so b is 2)
// a: 3 (calls b)

// Request types:
// leaf_req_t: just work
// c_req_t: { work, leaf_req }
// d_req_t: { work, leaf_req }
// b_req_t: { work, c_req, d_req }  // Both branches
// a_req_t: { work, b_req }
```

### Key Optimizations

**1. Tail Call Optimization**

When an async function ends with a single await, the completion can be forwarded directly:

```rust
async fn wrapper(x: u32) -> Result<Data> {
    await inner_operation(x)  // Tail call
}

// Instead of: wrapper_submit → inner_complete → wrapper_complete → parent
// Optimize to: wrapper_submit → inner_complete → parent (skip wrapper_complete)
```

**2. Inline Small Functions**

Trivial async functions can be inlined to eliminate request structures:

```rust
async fn trivial(x: u32) -> u32 {
    x + 1  // No async calls
}

// Inline at call site instead of generating machine
```

**3. Static Select Analysis**

When select branches are known at compile time, optimize:

```rust
select {
    result = await operation => Ok(result),
    _ = await timer(INFINITE) => Err(Error::Timeout),  // Never happens
}

// Optimize: don't submit timer, just submit operation
```

**4. Dead Code Elimination**

Remove unused operations and unreachable branches:

```rust
async fn conditional(flag: bool) -> Result<()> {
    if flag {
        await operation_a()
    } else {
        await operation_b()
    }
}

// If flag is statically known, eliminate unused branch
```

### Debug Support

**Debug builds include:**
- Assertions for requester chain validation
- Work state transition checks
- Stack overflow detection for nested requests
- Tracing instrumentation at each await point

**Example debug instrumentation:**

```c
void debug_submit(void* state, kwork_t* work) {
    assert(work != NULL);
    assert(work->requester != NULL);
    assert(work->callback != NULL);
    assert(work->state == KWORK_STATE_DEAD);

    TRACE("submit: op=%u requester=%p", work->op, work->requester);

    actual_submit(state, work);
}
```

---

## Conclusion

### Summary of Benefits

This language design provides:

✅ **Ergonomic syntax** - Familiar async/await, select expressions, clean error handling
✅ **Zero allocation** - Compile-time async stack embedding eliminates runtime allocation
✅ **Type safety** - Effect types prevent mixing async/sync, machine interfaces ensure contracts
✅ **Performance** - Compiles to efficient machine pattern C code with no overhead
✅ **Composability** - Machine interfaces enable vertical composition and swapping
✅ **Portability** - Same code runs on bare metal and hosted platforms
✅ **Debuggability** - Explicit state machines make debugging easier than callback hell

### How It Achieves Ergonomics + Performance

The key insight is **compile-time analysis and transformation**:

1. **Programmer writes** high-level async/await code
2. **Compiler analyzes** call graphs and computes depths
3. **Compiler generates** request structures with embedded children
4. **Runtime executes** with zero allocation during delegation

This achieves the **ergonomics of high-level async languages** (JavaScript, Python, C#) with the **performance of low-level manual state machines** (C, Rust).

### Comparison to Manual State Machines

**Manual state machine** (current VMOS style):
```c
// Programmer writes:
void operation_submit(void* state, kwork_t* work) {
    req_t* req = CONTAINER_OF(work, req_t, work);
    req->child_req.work.requester = &machine;
    // ... lots of boilerplate
    child_machine.submit(&child_machine, &req->child_req.work);
}
```

**This language:**
```rust
// Programmer writes:
async fn operation() {
    await child_operation()
}

// Compiler generates all the boilerplate
```

The language **eliminates boilerplate** while maintaining the same performance characteristics.

### Use Cases

This language is ideal for:

- **Kernel development** - Zero-allocation async I/O
- **Embedded systems** - Predictable memory usage, no GC
- **Real-time systems** - Bounded latency, no dynamic allocation
- **High-performance networking** - Zero-copy async operations
- **Device drivers** - Clean async hardware interactions
- **Bare-metal applications** - No OS dependencies

### Future Directions

Potential extensions:

1. **Effect inference** - Infer async vs sync effects automatically
2. **Linear types** - Guarantee single ownership of work items
3. **Automatic cancellation** - Cancel child operations on early return
4. **Better error ergonomics** - Context-aware error messages
5. **REPL / interpreter** - Interactive development and testing
6. **IDE support** - Syntax highlighting, autocomplete, inline error checking
7. **Formal verification** - Prove state machine properties

### Final Thoughts

This language bridges the gap between **high-level async programming** and **low-level system performance**. It makes the machine pattern accessible and ergonomic while preserving its **zero-allocation property**.

By moving complexity from runtime to compile time, the language achieves:
- **Fast execution** - No allocation overhead
- **Small binaries** - No runtime library bloat
- **Easy debugging** - Explicit state machines
- **Safe composition** - Type-checked interfaces

The machine pattern, combined with this language design, provides a **compelling foundation for next-generation systems programming**.

---

*End of document.*

# VMOS Hosted Platform Architecture

This document describes the architecture and design for hosted VMOS platforms
that run as user-space applications on top of existing operating systems,
starting with macOS as the reference implementation.

## Overview

### Bare-Metal vs. Hosted Platforms

VMOS currently supports **bare-metal platforms** (arm64, arm32, x64, rv64,
rv32) that run directly on QEMU virtual hardware. These platforms:
- Control hardware directly (interrupts, timers, MMU)
- Use VirtIO virtual devices (PCI or MMIO transport)
- Run in supervisor/kernel mode with full hardware access
- Implement platform.h by programming hardware registers

A **hosted platform** runs as a normal user-space process on an existing OS
kernel and:
- Uses OS system calls instead of hardware access
- Uses native OS facilities (files, sockets, timers) instead of VirtIO
- Runs in user mode without direct hardware access
- Implements platform.h by calling OS APIs

### Goals

The hosted platform aims to:
1. **Reuse the entire VMOS event loop and work queue system** - no changes to
   kernel/kmain.c or kernel/kernel.c
2. **Provide the same user API** - kapi.h semantics remain identical for
   applications
3. **Support native OS I/O** - files, sockets, /dev/urandom instead of VirtIO
   devices
4. **Enable rapid development** - no QEMU required, native debugging tools work
5. **Demonstrate portability** - prove the platform abstraction works beyond
   bare metal

### macOS Reference Platform

macOS is the first hosted platform, using:
- **kqueue** for unified event dispatch (timers, file I/O, network I/O)
- **Native file I/O** replacing VirtIO block device
- **BSD sockets** replacing VirtIO network device
- **arc4random_buf()** or /dev/urandom replacing VirtIO RNG
- **mach_absolute_time()** for monotonic clock

## Architecture Analysis

### What Stays the Same

The following code is **completely reusable** without modification:

```
kernel/kernel.c          - Work queue state machine (1000+ lines)
kernel/kmain.c           - Event loop (53 lines)
kernel/kapi.h            - API definitions
kernel/platform.h        - Contract only (no implementation)
```

The event loop pattern is unchanged:
```c
while (1) {
  kmain_tick(k, k->current_time_ns);           // Process work
  ktime_t timeout = kmain_next_delay(k);       // Next timer
  k->current_time_ns = platform_wfi(&k->platform, timeout);  // Wait
}
```

### What Changes Completely

Platform implementations must be rewritten:

```
platform/macos/          - New directory (analogous to platform/arm64/)
  platform_impl.h        - Platform-specific types
  platform_init.c        - Initialization
  platform_wfi.c         - Event dispatch loop
  timer.c                - Time source
  kqueue_events.c        - kqueue event management
  file_io.c              - File operations (replaces VirtIO block)
  network.c              - Socket operations (replaces VirtIO net)
  rng.c                  - Random number generation (replaces VirtIO RNG)
  debug.c                - Logging and debug output
  mem.c                  - Memory region management
  platform.mk            - Build configuration
```

### Key Architectural Differences

| Aspect | Bare-Metal (QEMU) | Hosted (macOS) |
|--------|-------------------|----------------|
| **Event Source** | Hardware interrupts (GIC/IOAPIC) | kqueue events |
| **Timer** | Hardware timer (Generic Timer, LAPIC) | kevent() timeout |
| **Block I/O** | VirtIO block device (PCI/MMIO) | POSIX file operations + threadpool |
| **Network I/O** | VirtIO network device | BSD sockets + kqueue EVFILT_READ/WRITE |
| **RNG** | VirtIO RNG device | arc4random_buf() |
| **IRQ Dispatch** | Exception vectors → ISR → deferred processing | kqueue event loop → callback |
| **Memory** | Physical memory from FDT/ACPI -> custom allocators | malloc/mmap -> custom allocators |
| **Synchronization** | Interrupt disable/enable | Not needed (single-threaded event loop) |
| **Wait for Interrupt** | WFI/HLT instruction | kevent() blocking call |

## Layered State Machine Architecture

### Conceptual Model: Message-Passing State Machines

The fundamental insight is that VMOS can be viewed as a hierarchy of
**communicating state machines**, where each layer accepts high-level requests
and either:
1. **Handles them directly** (if primitives are available), or
2. **Translates them to lower-level requests** and delegates to a sublayer

This creates a flexible architecture where:
- **Hosted platforms** have shallow layer stacks (OS provides high-level primitives)
- **Bare-metal platforms** have deep layer stacks (must build primitives from hardware)

### Three-Layer Conceptual Model

```
┌──────────────────────────────────────────────┐
│  User Machine (Application Layer)            │
│  - Submits: UDP send/recv, file I/O, timers  │
│  - Receives: Completion callbacks            │
└──────────────────┬───────────────────────────┘
                   │ ksubmit(work)
                   │ callback(work)
                   ▼
┌──────────────────────────────────────────────┐
│  Runtime Machine (Kernel Layer)              │
│  - Manages: Work queue state machine         │
│  - Routes: Work to platform by operation type│
│  - Tracks: DEAD→SUBMIT→LIVE→READY→callback   │
└──────────────────┬───────────────────────────┘
                   │ platform_submit(work)
                   │ kplatform_complete_work(work)
                   ▼
┌──────────────────────────────────────────────┐
│  Platform Machine (Hardware/OS Abstraction)  │
│  - Accepts: High-level requests (UDP, files) │
│  - Strategy depends on platform capabilities  │
└──────────────────┬───────────────────────────┘
                   │
        ┌──────────┴──────────┐
        ▼                     ▼
┌──────────────────┐  ┌──────────────────────┐
│  macOS Platform  │  │  Bare-Metal Platform │
│  (shallow stack) │  │  (deep stack)        │
└──────────────────┘  └──────────┬───────────┘
                                 │
                      Delegates to Subplatform
                                 ▼
                      ┌──────────────────────┐
                      │  Network Stack       │
                      │  (IP/UDP/ARP/ICMP)   │
                      └──────────┬───────────┘
                                 │
                      Delegates to VirtIO Layer
                                 ▼
                      ┌──────────────────────┐
                      │  Ethernet Frames     │
                      │  (VirtIO Net Device) │
                      └──────────────────────┘
```

### Layer Responsibilities

#### Layer 1: User Machine (Application)
**Interface**: kapi.h (ksubmit, kcancel, callbacks)

**Responsibilities**:
- Create work requests with application-level semantics
- Submit work to runtime
- Handle completion callbacks
- Manage application state

**Example**:
```c
// User wants to send a UDP packet
knet_send_req_t req;
kwork_init(&req.work, KWORK_OP_NET_SEND, ctx, callback, 0);
req.packets = &packet;
req.num_packets = 1;
// Packet contains: dest IP, dest port, payload
ksubmit(kernel, &req.work);
```

#### Layer 2: Runtime Machine (Kernel)
**Interface**: kernel.h (kmain_tick, kplatform_complete_work)

**Responsibilities**:
- State machine management (DEAD→SUBMIT→LIVE→READY)
- Timer heap management
- Bulk work submission to platform
- Callback invocation
- **Does not interpret work semantics** - just routes by operation type (handles timer, submits everything else to platform)

**Example**:
```c
// In kmain_tick():
// 1. Collect all pending work
// 2. Call platform_submit(submissions, cancellations)
// 3. Platform eventually calls kplatform_complete_work()
// 4. Invoke user callback
```

#### Layer 3: Platform Machine (OS/Hardware Abstraction)
**Interface**: platform.h (platform_submit, platform_tick, platform_wfi)

**Responsibilities**:
- **Interpret work semantics** (what does KWORK_OP_NET_SEND mean?)
- Route to appropriate subsystem or sublayer
- Manage event sources (interrupts/kqueue/epoll)
- Call kplatform_complete_work() when operations finish

**Two strategies**:

**Strategy A: Direct handling** (macOS)
```c
void platform_submit(platform_t *p, kwork_t *submissions, kwork_t *cancellations) {
  for (kwork_t *w = submissions; w; w = w->next) {
    if (w->op == KWORK_OP_NET_SEND) {
      knet_send_req_t *req = CONTAINER_OF(w, knet_send_req_t, work);
      // OS provides UDP directly - handle immediately
      sendto(p->udp_socket, req->packets[0].buffer, ...);
      w->state = KWORK_STATE_LIVE;
      kplatform_complete_work(p->kernel, w, KERR_OK);
    }
  }
}
```

**Strategy B: Delegate to subplatform** (bare-metal)
```c
void platform_submit(platform_t *p, kwork_t *submissions, kwork_t *cancellations) {
  for (kwork_t *w = submissions; w; w = w->next) {
    if (w->op == KWORK_OP_NET_SEND) {
      // Delegate to network stack sublayer
      netstack_submit_udp(p->netstack, w);
    }
  }
}
```

#### Layer 4: Subplatform (Protocol Stacks / Device Drivers)
**Interface**: Platform-specific (not in platform.h)

**Responsibilities** (bare-metal network example):
- Implement higher-level protocols from lower-level primitives
- Maintain protocol state (ARP cache, routing table, fragmentation)
- Translate high-level operations to device operations
- Call platform completion when done

**Example**:
```c
void netstack_submit_udp(netstack_t *stack, kwork_t *work) {
  knet_send_req_t *req = CONTAINER_OF(work, knet_send_req_t, work);

  // Parse destination IP from packet
  ip_addr_t dest_ip = parse_dest_ip(req->packets[0].buffer);

  // ARP lookup for MAC address
  if (!arp_cache_lookup(stack->arp, dest_ip, &dest_mac)) {
    // Need to do ARP first
    arp_send_request(stack, dest_ip, work);  // Will complete work later
    return;
  }

  // Build Ethernet frame
  ethernet_frame_t frame;
  build_udp_packet(&frame, req, dest_mac);

  // Submit to VirtIO layer
  virtio_net_submit_frame(stack->virtio_net, &frame, work);
}
```

### Message Flow Examples

#### Example 1: UDP Send on macOS (Shallow Stack)

```
[User Layer]
  ksubmit(KWORK_OP_NET_SEND) with dest IP:port, payload
    ↓
[Runtime Layer]
  State: DEAD → SUBMIT_REQUESTED
  Enqueue to submit_queue
  In next kmain_tick():
    ↓
[Platform Layer - macOS]
  platform_submit() receives work
  Parse dest IP:port from buffer
  Call sendto(socket, dest_ip, dest_port, payload)
  OS handles UDP/IP/Ethernet/ARP
  Mark work LIVE
  Immediately call kplatform_complete_work(KERR_OK)
    ↓
[Runtime Layer]
  State: LIVE → READY
  In next kmain_tick():
    Call user callback
    State: READY → DEAD
```

**Key insight**: Platform layer has direct access to UDP primitive, so it
handles work in one step.

#### Example 2: UDP Send on Bare-Metal (Deep Stack)

```
[User Layer]
  ksubmit(KWORK_OP_NET_SEND) with dest IP:port, payload
    ↓
[Runtime Layer]
  State: DEAD → SUBMIT_REQUESTED
  Enqueue to submit_queue
  In next kmain_tick():
    ↓
[Platform Layer - Bare-Metal]
  platform_submit() receives work
  Delegate to netstack sublayer:
    netstack_submit_udp(work)
      ↓
[Subplatform Layer - Network Stack]
  netstack_submit_udp():
    Parse dest IP from buffer
    Check ARP cache for dest IP
      ↓
  [Path A: ARP cache hit]
    Build UDP header (src/dest port, checksum)
    Build IP header (src/dest IP, TTL, checksum)
    Build Ethernet header (src/dest MAC, ethertype=0x0800)
    Delegate to VirtIO layer:
      virtio_net_submit_frame(ethernet_frame, work)
        ↓
  [Path B: ARP cache miss]
    Store work in pending_arp table
    Build ARP request packet
    Submit ARP request to VirtIO layer
      ↓
      (Later: ARP response arrives)
      ARP interrupt → process ARP response
      Update ARP cache
      Resume original work:
        Build UDP/IP/Ethernet headers
        Submit to VirtIO layer
          ↓
[Subplatform Layer - VirtIO Driver]
  virtio_net_submit_frame():
    Allocate descriptor in virtqueue
    Copy ethernet_frame to descriptor buffer
    Add to available ring
    Ring doorbell (write to notify register)
    Store work pointer in descriptor tracking
      ↓
  (Later: VirtIO device processes frame and generates interrupt)
  Interrupt → virtio_net_irq_handler()
    Enqueue device to IRQ ring
      ↓
  Next platform_tick():
    Process IRQ ring
    virtio_net_process_irq():
      Check used ring
      Find completed descriptor
      Retrieve work pointer
      Call kplatform_complete_work(work, KERR_OK)
        ↓
[Runtime Layer]
  State: LIVE → READY
  In next kmain_tick():
    Call user callback
    State: READY → DEAD
```

**Key insight**: Platform layer delegates through multiple sublayers (network stack → VirtIO driver) because bare-metal doesn't have UDP primitive.

### Sublayer API Design

To support this architecture, we need well-defined internal interfaces between platform and subplatform layers.

#### Option 1: Direct Function Calls (Current Approach)
```c
// In platform/arm64/platform_submit.c
void platform_submit(platform_t *p, kwork_t *submissions, ...) {
  // Directly call device-specific functions
  virtio_net_submit_work(p->virtio_net_ptr, net_work, p->kernel);
}
```

**Pros**: Simple, direct, efficient
**Cons**: Tight coupling, hard to add protocol layers

#### Option 2: Sublayer State Machine Interface
```c
// New abstraction: sublayer_t
typedef struct sublayer sublayer_t;

struct sublayer {
  const char *name;
  void (*submit)(sublayer_t *layer, kwork_t *work);
  void (*tick)(sublayer_t *layer, kernel_t *k);
  void (*complete)(sublayer_t *layer, kwork_t *work, kerr_t result);
  sublayer_t *next_layer;  // Delegation target
};

// Platform owns a stack of sublayers
struct platform {
  // ...
  sublayer_t *network_layer_stack;  // UDP → IP → Ethernet → VirtIO
};

// Platform delegates to top of stack
void platform_submit(platform_t *p, kwork_t *submissions, ...) {
  for (kwork_t *w = submissions; w; w = w->next) {
    if (w->op == KWORK_OP_NET_SEND) {
      p->network_layer_stack->submit(p->network_layer_stack, w);
    }
  }
}

// Network stack layer
void netstack_submit(sublayer_t *layer, kwork_t *work) {
  netstack_t *stack = CONTAINER_OF(layer, netstack_t, sublayer);

  // Process UDP semantics
  // ...

  // Delegate to next layer (VirtIO)
  layer->next_layer->submit(layer->next_layer, work);
}

// VirtIO layer
void virtio_net_submit(sublayer_t *layer, kwork_t *work) {
  virtio_net_dev_t *dev = CONTAINER_OF(layer, virtio_net_dev_t, sublayer);

  // Add to virtqueue
  // Ring doorbell
  // ...
}
```

**Pros**: Clean separation, easy to insert layers, testable
**Cons**: Additional indirection, more complex

#### Option 3: Message Queue Between Layers
```c
// Each layer has input and output message queues
struct sublayer {
  kwork_t *input_queue;   // Work coming from above
  kwork_t *output_queue;  // Work going below
  void (*process)(sublayer_t *layer);
};

// Platform tick processes each layer
void platform_tick(platform_t *p, kernel_t *k) {
  // Process layers top-down
  netstack_process(p->netstack);      // Consumes input, produces output
  virtio_net_process(p->virtio_net);  // Consumes netstack output

  // Process interrupts bottom-up
  virtio_net_process_irq(p->virtio_net);  // Produces completions
  netstack_process_completions(p->netstack);  // Routes to runtime
}
```

**Pros**: Decoupled, explicit data flow, easy to reason about
**Cons**: Requires queue management, potential latency

### Concrete Example: Network Stack Sublayer

Let's design what the network stack sublayer would look like for bare-metal platforms.

#### Data Structures

```c
// Network stack state (in platform_impl.h for bare-metal)
typedef struct {
  // ARP cache
  struct {
    ip_addr_t ip;
    mac_addr_t mac;
    uint64_t timestamp_ns;
    bool valid;
  } arp_cache[ARP_CACHE_SIZE];

  // Pending work waiting for ARP
  struct {
    kwork_t *work;
    ip_addr_t dest_ip;
  } pending_arp[MAX_PENDING_ARP];

  // IP fragmentation state (if needed)
  // Routing table (if needed)

  // Device configuration
  ip_addr_t local_ip;
  mac_addr_t local_mac;
  ip_addr_t gateway_ip;
  ip_addr_t netmask;

  // Pointer to VirtIO device for delegation
  virtio_net_dev_t *virtio_net;

  // Back pointer to platform for completion
  platform_t *platform;
} netstack_t;

// Extended platform_t for bare-metal
struct platform {
  // ... existing fields ...

  // Network stack sublayer
  netstack_t netstack;
};
```

#### UDP Send Implementation

```c
void netstack_submit_udp_send(netstack_t *stack, knet_send_req_t *req) {
  // Parse destination from buffer (assume first 6 bytes are IP:port)
  ip_addr_t dest_ip;
  uint16_t dest_port;
  parse_udp_dest(req->packets[0].buffer, &dest_ip, &dest_port);

  // Lookup destination MAC
  mac_addr_t dest_mac;
  if (!netstack_arp_lookup(stack, dest_ip, &dest_mac)) {
    // Need to do ARP first
    netstack_initiate_arp(stack, dest_ip, &req->work);
    req->work.state = KWORK_STATE_LIVE;
    return;
  }

  // Build packet layers
  ethernet_frame_t frame;
  netstack_build_udp_packet(stack, req, dest_ip, dest_port,
                           dest_mac, &frame);

  // Delegate to VirtIO layer
  virtio_net_send_frame(stack->virtio_net, &frame, &req->work);
  req->work.state = KWORK_STATE_LIVE;
}

void netstack_build_udp_packet(netstack_t *stack, knet_send_req_t *req,
                               ip_addr_t dest_ip, uint16_t dest_port,
                               mac_addr_t dest_mac, ethernet_frame_t *frame) {
  uint8_t *ptr = frame->buffer;

  // Ethernet header (14 bytes)
  memcpy(ptr + 0, dest_mac, 6);           // Dest MAC
  memcpy(ptr + 6, stack->local_mac, 6);   // Src MAC
  *(uint16_t*)(ptr + 12) = htons(0x0800); // EtherType: IPv4
  ptr += 14;

  // IP header (20 bytes, no options)
  ptr[0] = 0x45;                           // Version 4, IHL 5
  ptr[1] = 0x00;                           // DSCP/ECN
  uint16_t total_len = 20 + 8 + req->packets[0].buffer_size;
  *(uint16_t*)(ptr + 2) = htons(total_len); // Total length
  *(uint16_t*)(ptr + 4) = htons(0);        // Identification
  *(uint16_t*)(ptr + 6) = htons(0x4000);   // Flags: DF
  ptr[8] = 64;                             // TTL
  ptr[9] = 17;                             // Protocol: UDP
  *(uint16_t*)(ptr + 10) = 0;              // Checksum (calculate later)
  *(uint32_t*)(ptr + 12) = htonl(stack->local_ip);  // Src IP
  *(uint32_t*)(ptr + 16) = htonl(dest_ip);          // Dest IP
  *(uint16_t*)(ptr + 10) = ip_checksum(ptr, 20);    // Fill checksum
  ptr += 20;

  // UDP header (8 bytes)
  uint16_t src_port = 12345;  // Could be from request or platform state
  *(uint16_t*)(ptr + 0) = htons(src_port);          // Src port
  *(uint16_t*)(ptr + 2) = htons(dest_port);         // Dest port
  uint16_t udp_len = 8 + req->packets[0].buffer_size;
  *(uint16_t*)(ptr + 4) = htons(udp_len);           // Length
  *(uint16_t*)(ptr + 6) = 0;                        // Checksum (optional for IPv4)
  ptr += 8;

  // Payload
  memcpy(ptr, req->packets[0].buffer, req->packets[0].buffer_size);

  frame->length = 14 + 20 + 8 + req->packets[0].buffer_size;
}
```

#### ARP Handling

```c
void netstack_initiate_arp(netstack_t *stack, ip_addr_t dest_ip, kwork_t *work) {
  // Store work in pending table
  for (int i = 0; i < MAX_PENDING_ARP; i++) {
    if (stack->pending_arp[i].work == NULL) {
      stack->pending_arp[i].work = work;
      stack->pending_arp[i].dest_ip = dest_ip;
      break;
    }
  }

  // Build ARP request
  ethernet_frame_t arp_frame;
  uint8_t *ptr = arp_frame.buffer;

  // Ethernet header
  memset(ptr + 0, 0xFF, 6);                // Broadcast MAC
  memcpy(ptr + 6, stack->local_mac, 6);    // Src MAC
  *(uint16_t*)(ptr + 12) = htons(0x0806);  // EtherType: ARP
  ptr += 14;

  // ARP header
  *(uint16_t*)(ptr + 0) = htons(1);        // Hardware type: Ethernet
  *(uint16_t*)(ptr + 2) = htons(0x0800);   // Protocol type: IPv4
  ptr[4] = 6;                              // Hardware size
  ptr[5] = 4;                              // Protocol size
  *(uint16_t*)(ptr + 6) = htons(1);        // Opcode: Request
  memcpy(ptr + 8, stack->local_mac, 6);    // Sender MAC
  *(uint32_t*)(ptr + 14) = htonl(stack->local_ip);  // Sender IP
  memset(ptr + 18, 0, 6);                  // Target MAC (unknown)
  *(uint32_t*)(ptr + 24) = htonl(dest_ip); // Target IP

  arp_frame.length = 14 + 28;

  // Send ARP request (doesn't track completion, fire and forget)
  virtio_net_send_frame_immediate(stack->virtio_net, &arp_frame);
}

void netstack_process_arp_response(netstack_t *stack, uint8_t *frame) {
  uint8_t *arp = frame + 14;

  // Verify it's a response
  uint16_t opcode = ntohs(*(uint16_t*)(arp + 6));
  if (opcode != 2) return;  // Not a response

  // Extract sender IP and MAC
  ip_addr_t sender_ip = ntohl(*(uint32_t*)(arp + 14));
  mac_addr_t sender_mac;
  memcpy(sender_mac, arp + 8, 6);

  // Update ARP cache
  netstack_arp_update(stack, sender_ip, sender_mac);

  // Resume any pending work waiting for this IP
  for (int i = 0; i < MAX_PENDING_ARP; i++) {
    if (stack->pending_arp[i].work != NULL &&
        stack->pending_arp[i].dest_ip == sender_ip) {
      kwork_t *work = stack->pending_arp[i].work;
      stack->pending_arp[i].work = NULL;

      // Retry the original send
      knet_send_req_t *req = CONTAINER_OF(work, knet_send_req_t, work);
      netstack_submit_udp_send(stack, req);
    }
  }
}
```

#### UDP Receive Implementation

```c
void netstack_process_received_frame(netstack_t *stack, uint8_t *frame,
                                     size_t length, knet_recv_req_t *recv_req) {
  // Validate Ethernet header
  uint16_t ethertype = ntohs(*(uint16_t*)(frame + 12));

  if (ethertype == 0x0806) {
    // ARP packet
    netstack_process_arp_response(stack, frame);
    return;  // Don't complete user work for ARP
  }

  if (ethertype != 0x0800) {
    return;  // Not IPv4
  }

  // Parse IP header
  uint8_t *ip = frame + 14;
  uint8_t protocol = ip[9];

  if (protocol != 17) {
    return;  // Not UDP
  }

  // Parse UDP header
  uint8_t *udp = ip + 20;  // Assume no IP options
  uint16_t src_port = ntohs(*(uint16_t*)(udp + 0));
  uint16_t dst_port = ntohs(*(uint16_t*)(udp + 2));
  uint16_t udp_len = ntohs(*(uint16_t*)(udp + 4));

  // Extract payload
  uint8_t *payload = udp + 8;
  size_t payload_len = udp_len - 8;

  // Copy to user buffer (add metadata if needed)
  size_t buffer_idx = recv_req->buffer_index;
  knet_buffer_t *buf = &recv_req->buffers[buffer_idx];

  // Could store src IP:port in buffer or extend API
  memcpy(buf->buffer, payload, MIN(payload_len, buf->buffer_size));
  buf->packet_length = payload_len;

  // Complete the receive request
  kplatform_complete_work(stack->platform->kernel, &recv_req->work, KERR_OK);
}
```

### Integration with Existing Platform

#### Bare-Metal Platform Submit

```c
// In platform/arm64/platform_virtio.c (or new platform_network.c)
void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations) {
  // Separate work by type
  kwork_t *rng_work = NULL;
  kwork_t *blk_work = NULL;
  kwork_t *net_send_work = NULL;
  kwork_t *net_recv_work = NULL;

  for (kwork_t *w = submissions; w != NULL; w = w->next) {
    switch (w->op) {
      case KWORK_OP_RNG_READ:
        // Link to RNG list
        break;
      case KWORK_OP_BLOCK_READ:
      case KWORK_OP_BLOCK_WRITE:
      case KWORK_OP_BLOCK_FLUSH:
        // Link to block list
        break;
      case KWORK_OP_NET_SEND:
        // Delegate to network stack
        for (kwork_t *w2 = w; w2; w2 = w2->next) {
          if (w2->op == KWORK_OP_NET_SEND) {
            knet_send_req_t *req = CONTAINER_OF(w2, knet_send_req_t, work);
            netstack_submit_udp_send(&platform->netstack, req);
          }
        }
        break;
      case KWORK_OP_NET_RECV:
        // Standing work: register with VirtIO receive
        netstack_submit_udp_recv(&platform->netstack,
                                CONTAINER_OF(w, knet_recv_req_t, work));
        break;
    }
  }

  // Handle RNG, block work as before
  if (rng_work && platform->virtio_rng_ptr) {
    virtio_rng_submit_work(platform->virtio_rng_ptr, rng_work, platform->kernel);
  }
  // ...
}
```

#### Bare-Metal Platform Tick

```c
void platform_tick(platform_t *platform, kernel_t *k) {
  // Process deferred VirtIO interrupts
  void *dev_ptr;
  while ((dev_ptr = kirq_ring_dequeue_bounded(&platform->irq_ring, 16)) != NULL) {
    kdevice_base_t *dev = (kdevice_base_t *)dev_ptr;

    if (dev == (kdevice_base_t *)platform->virtio_net_ptr) {
      // Network device interrupt - may be Ethernet frame
      virtio_net_process_irq(platform->virtio_net_ptr, k, &platform->netstack);
    } else if (dev == (kdevice_base_t *)platform->virtio_rng_ptr) {
      // RNG interrupt
      dev->process_irq(dev, k);
    } else {
      // Other devices
      dev->process_irq(dev, k);
    }
  }
}

void virtio_net_process_irq(virtio_net_dev_t *dev, kernel_t *k,
                           netstack_t *netstack) {
  // Check TX completions
  while (virtqueue_has_used(&dev->tx_vq)) {
    uint16_t desc_idx;
    uint32_t len;
    virtqueue_get_used(&dev->tx_vq, &desc_idx, &len);

    // Find work for this descriptor
    knet_send_req_t *req = find_tx_work(dev, desc_idx);
    if (req) {
      // TX complete - no additional processing needed
      kplatform_complete_work(k, &req->work, KERR_OK);
    }

    virtqueue_free_desc(&dev->tx_vq, desc_idx);
  }

  // Check RX completions
  while (virtqueue_has_used(&dev->rx_vq)) {
    uint16_t desc_idx;
    uint32_t len;
    virtqueue_get_used(&dev->rx_vq, &desc_idx, &len);

    // Find receive request
    knet_recv_req_t *req = find_rx_work(dev, desc_idx);
    if (req) {
      // Get frame from descriptor buffer
      uint8_t *frame = virtqueue_get_buffer(&dev->rx_vq, desc_idx);

      // Process through network stack (UDP/IP/Ethernet parsing)
      netstack_process_received_frame(netstack, frame, len, req);

      // Note: completion called by netstack if it's a valid UDP packet
    }

    // Re-add descriptor for next receive
    virtqueue_add_desc(&dev->rx_vq, desc_idx, ...);
  }
}
```

### API Evolution

This layered architecture suggests potential API evolution:

#### Current API (Layer 3: Platform Level)
```c
// User submits UDP send with destination embedded in buffer
kwork_init(&req.work, KWORK_OP_NET_SEND, ...);
req.packets[0].buffer = [dest_ip, dest_port, payload];
```

#### Enhanced API (Explicit Addressing)
```c
// Extend knet_send_req_t with addressing
typedef struct {
  kwork_t work;
  knet_buffer_t *packets;
  size_t num_packets;
  // NEW: Explicit addressing
  uint32_t dest_ip;     // Network byte order
  uint16_t dest_port;   // Network byte order
  uint16_t src_port;    // 0 = auto-assign
  knet_send_req_platform_t platform;
} knet_send_req_t;
```

This makes the network stack's job easier and makes the API more explicit.

### Summary: Layered Architecture Benefits

1. **Separation of Concerns**
   - User: Application logic
   - Runtime: State management
   - Platform: OS/hardware abstraction
   - Subplatform: Protocol implementation

2. **Platform Flexibility**
   - Hosted: Shallow stack (OS provides primitives)
   - Bare-metal: Deep stack (implement from hardware up)

3. **Code Reuse**
   - Network stack layer could be shared across bare-metal platforms
   - VirtIO drivers shared across architectures
   - Runtime completely platform-independent

4. **Testability**
   - Can test network stack in isolation with mock VirtIO
   - Can test application with mock network stack
   - Clear interfaces at each boundary

5. **Extensibility**
   - Add TCP by inserting TCP layer above IP layer
   - Add TLS by inserting above TCP
   - Add custom protocols without changing runtime

The key insight is that **the platform layer is itself decomposable** into sublayers, and this decomposition varies by platform capability. Hosted platforms delegate to the OS; bare-metal platforms implement the missing layers themselves.

## Unified State Machine API Pattern

### Motivation: API Asymmetry

Currently, VMOS uses different API patterns at different layer boundaries:

**User → Runtime** (kapi.h):
```c
// Work-based async pattern
kwork_t work;
kwork_init(&work, KWORK_OP_NET_SEND, ctx, callback, flags);
ksubmit(kernel, &work);
// Later: callback invoked
```

**Runtime → Platform** (platform.h):
```c
// Direct function call pattern
platform_submit(platform, submissions, cancellations);
platform_tick(platform, kernel);
ktime_t wakeup = platform_wfi(platform, timeout);
```

**Platform → Subplatform** (bare-metal only):
```c
// Ad-hoc device-specific pattern
virtio_net_submit_work(dev, work, kernel);
netstack_submit_udp(stack, work);
```

This asymmetry raises the question: **What if we used the same work-based pattern everywhere?** The kwork_t structure and submission protocol are already generic—only the operation code space changes between layers.

### Abstract Machine Interface

Every layer in VMOS is fundamentally a **state machine** that:
1. **Accepts work** - receives operation requests from above
2. **Processes work** - performs operations or delegates to layers below
3. **Completes work** - notifies upper layer when operations finish

We can formalize this as an abstract machine interface:

```c
typedef struct machine machine_t;

struct machine {
    const char *name;                    // For debugging
    void *state;                         // Machine-specific state
    machine_t *parent;                   // Parent machine (for completions)

    // Core operations
    void (*submit)(machine_t *m, kwork_t *submissions, kwork_t *cancellations);
    void (*tick)(machine_t *m);          // Process pending work
    ktime_t (*wait)(machine_t *m, ktime_t timeout_ns);  // Block for events

    // Operation code space
    uint32_t num_ops;                    // Number of valid operations
    const char **op_names;               // Operation names (for debugging)

    // Optional delegation
    machine_t **sublayers;               // Stack of sublayers
    size_t num_sublayers;
};
```

Every machine implements the same protocol but operates on a **different operation code space**. The operation codes are just integers, but each layer interprets them differently.

### Per-Layer Operation Spaces

Each machine defines its own valid operations:

```c
// User machine (application-defined)
enum user_op {
    USER_OP_HTTP_REQUEST = 0,
    USER_OP_DATABASE_QUERY = 1,
    USER_OP_FILE_PARSE = 2,
    // Application-specific operations
};

// Runtime machine (kapi.h - existing)
enum runtime_op {
    KWORK_OP_TIMER = 0,
    KWORK_OP_RNG_READ = 1,
    KWORK_OP_BLOCK_READ = 2,
    KWORK_OP_BLOCK_WRITE = 3,
    KWORK_OP_BLOCK_FLUSH = 4,
    KWORK_OP_NET_SEND = 5,
    KWORK_OP_NET_RECV = 6,
};

// Platform machine (NEW - would replace direct platform.h functions)
enum platform_op {
    PLATFORM_OP_TIMER = 0,           // Register timer event
    PLATFORM_OP_IRQ = 1,             // Register IRQ handler
    PLATFORM_OP_MMIO_READ = 2,       // Read MMIO register
    PLATFORM_OP_MMIO_WRITE = 3,      // Write MMIO register
    PLATFORM_OP_PCI_CONFIG_READ = 4, // PCI config space read
    PLATFORM_OP_DMA_MAP = 5,         // Map memory for DMA
    PLATFORM_OP_EVENT_WAIT = 6,      // Wait for events (WFI)
};

// Subplatform machine - Network stack (NEW)
enum netstack_op {
    NETSTACK_OP_UDP_SEND = 0,
    NETSTACK_OP_UDP_RECV = 1,
    NETSTACK_OP_TCP_CONNECT = 2,
    NETSTACK_OP_TCP_SEND = 3,
    NETSTACK_OP_ARP_LOOKUP = 4,
    NETSTACK_OP_ROUTE_LOOKUP = 5,
};

// Subplatform machine - VirtIO (NEW)
enum virtio_op {
    VIRTIO_OP_SEND_BUFFER = 0,
    VIRTIO_OP_RECV_BUFFER = 1,
    VIRTIO_OP_NOTIFY = 2,
    VIRTIO_OP_RESET = 3,
};
```

The key insight: **Operation codes are scoped to their machine**. A work item with `op=0` means different things at different layers.

### Message Flow with Unified API

Example: UDP send on bare-metal with 4-layer stack

```
[User Machine]
  work1.op = USER_OP_HTTP_REQUEST
  machine_submit(runtime, work1)
    ↓
[Runtime Machine]
  Parses HTTP request → needs network send
  work2.op = KWORK_OP_NET_SEND (runtime op space)
  machine_submit(platform, work2)
    ↓
[Platform Machine - Bare Metal]
  No native UDP → delegate to network stack
  work3.op = NETSTACK_OP_UDP_SEND (netstack op space)
  machine_submit(netstack, work3)
    ↓
[Network Stack Machine]
  Parse dest IP, ARP lookup, build UDP/IP/Ethernet headers
  work4.op = VIRTIO_OP_SEND_BUFFER (virtio op space)
  machine_submit(virtio_net, work4)
    ↓
[VirtIO Machine]
  Add buffer to virtqueue, ring doorbell
  (Hardware processes, interrupt fires)
  machine_complete(netstack, work4, KERR_OK)
    ↓
[Network Stack Machine]
  machine_complete(platform, work3, KERR_OK)
    ↓
[Platform Machine]
  machine_complete(runtime, work2, KERR_OK)
    ↓
[Runtime Machine]
  State: LIVE → READY
  Invoke user callback
  machine_complete(user, work1, KERR_OK)
```

Compare to macOS hosted (2-layer stack):
```
[User Machine]
  work1.op = USER_OP_HTTP_REQUEST
  machine_submit(runtime, work1)
    ↓
[Runtime Machine]
  work2.op = KWORK_OP_NET_SEND
  machine_submit(platform, work2)
    ↓
[Platform Machine - macOS]
  Has native UDP via BSD sockets
  sendto() → immediate completion
  machine_complete(runtime, work2, KERR_OK)
    ↓
[Runtime Machine]
  machine_complete(user, work1, KERR_OK)
```

The platform machine handles the same operation (KWORK_OP_NET_SEND) but **chooses different strategies**:
- Bare-metal: Delegate to sublayers
- Hosted: Handle directly with OS primitives

### Implementation Approaches

#### Approach 1: Full Unification

Replace platform.h function calls with work submission:

**Before** (current):
```c
// Runtime calls platform directly
platform_submit(platform, submissions, cancellations);
```

**After** (unified):
```c
// Runtime submits work to platform machine
for (kwork_t *w = submissions; w; w = w->next) {
    kwork_t platform_work;
    kwork_init(&platform_work, PLATFORM_OP_SUBMIT_WORK, w, platform_complete_cb, 0);
    machine_submit(&platform_machine, &platform_work);
}
```

**Challenges**:
1. **Memory allocation**: Who allocates child work items? Current design has user allocate once.
2. **Work nesting**: One user work spawns multiple platform work items. How to track parent-child?
3. **Synchronous operations**: MMIO reads, PCI config reads are inherently synchronous. Forcing async adds overhead.
4. **Lifecycle operations**: `platform_init()` and `platform_wfi()` don't map cleanly to work submission.

#### Approach 2: Hybrid API with Conceptual Uniformity

Keep different concrete APIs but document them as instances of the same pattern.

**Current APIs remain**:
```c
// User → Runtime
ksubmit(kernel, work);

// Runtime → Platform
platform_submit(platform, submissions, cancellations);

// Platform → Subplatform
netstack_submit(netstack, work);
```

**But document the shared pattern**:
- All accept work items (or work-like structures)
- All process asynchronously (or appear to)
- All complete via callback/completion function

**Benefits**:
- Keep existing efficient implementations
- Maintain simplicity at well-defined boundaries
- Recognize conceptual uniformity
- Clear pattern for extending with new layers

#### Approach 3: Uniform API for Sublayers Only

Keep User→Runtime and Runtime→Platform APIs as-is, but standardize sublayer interfaces:

```c
// Sublayer interface (for network stack, VirtIO drivers, etc.)
typedef struct sublayer_ops {
    void (*submit)(void *layer, kwork_t *work);
    void (*tick)(void *layer);
    void (*complete)(void *layer, kwork_t *work, kerr_t result);
} sublayer_ops_t;

typedef struct sublayer {
    const char *name;
    const sublayer_ops_t *ops;
    void *state;
    void *parent;  // For completions
} sublayer_t;
```

Platform can then compose arbitrary sublayer stacks:
```c
struct platform {
    // ...
    sublayer_t *network_stack[];  // Stack: [netstack, virtio_net]
    sublayer_t *storage_stack[];  // Stack: [filesystem, virtio_blk]
};
```

### Design Challenges

#### Challenge 1: Work Item Ownership and Allocation

**Problem**: If each layer creates child work items, who owns memory?

**Option A - User Allocates All Layers**:
```c
typedef struct {
    kwork_t user_work;      // User layer
    kwork_t runtime_work;   // Runtime layer
    kwork_t platform_work;  // Platform layer
    // Request data
} knet_send_req_t;
```
**Pro**: Single allocation, clear ownership
**Con**: Requires coordination on max layer depth, wastes memory

**Option B - Dynamic Allocation**:
```c
kwork_t *child = kmalloc(sizeof(kwork_t));
kwork_init(child, child_op, parent_work, completion_callback, 0);
machine_submit(child_machine, child);
```
**Pro**: Flexible layer depth
**Con**: Allocation overhead, fragmentation, need allocator in all contexts

**Option C - Current Approach (Keep It)**:
```c
// User allocates request once
knet_send_req_t req;  // Contains work item
ksubmit(kernel, &req.work);

// Runtime doesn't allocate new work - just calls platform_submit()
platform_submit(platform, submissions, cancellations);
```
**Pro**: Zero additional allocation, simple ownership
**Con**: API asymmetry between layers

#### Challenge 2: Operation Translation

**Problem**: How does runtime know KWORK_OP_NET_SEND maps to NETSTACK_OP_UDP_SEND?

**Solution A - Hardcoded Mapping**:
```c
void runtime_submit(machine_t *m, kwork_t *work) {
    switch (work->op) {
        case KWORK_OP_NET_SEND:
            // Create child work with platform op code
            platform_work.op = NETSTACK_OP_UDP_SEND;
            machine_submit(platform_machine, platform_work);
            break;
    }
}
```
**Pro**: Simple, explicit
**Con**: Tight coupling between layers

**Solution B - Operation Registry**:
```c
typedef struct {
    uint32_t parent_op;    // Runtime op code
    uint32_t child_op;     // Platform op code
    machine_t *handler;    // Which machine handles it
} op_mapping_t;

// Runtime registers mappings
runtime_register_op(KWORK_OP_NET_SEND, NETSTACK_OP_UDP_SEND, netstack_machine);
```
**Pro**: Flexible, composable
**Con**: Complex, runtime overhead

#### Challenge 3: Synchronous vs Asynchronous Operations

**Problem**: Some operations are inherently synchronous (MMIO reads, PCI config reads).

**Current approach**:
```c
uint32_t val = platform_mmio_read32(addr);  // Returns immediately
```

**Unified approach**:
```c
kwork_t read_work;
kmmio_read_req_t read_req = { .addr = addr, .value = &val };
kwork_init(&read_work, PLATFORM_OP_MMIO_READ, &read_req, callback, 0);
machine_submit(platform_machine, &read_work);
// Wait for callback...
callback(kwork_t *work) {
    uint32_t val = read_req.value;  // Finally have value
}
```

This adds complexity for no benefit. Synchronous operations should stay synchronous.

**Recommendation**: Distinguish between:
- **Control plane** operations (setup, config) → can be synchronous functions
- **Data plane** operations (I/O, timers) → should be async work items

#### Challenge 4: 1-to-N Work Mapping

**Problem**: One high-level operation may require multiple low-level operations.

Example: UDP send with ARP:
```
User work: Send UDP packet
  ↓
Runtime work: KWORK_OP_NET_SEND
  ↓
Platform work: NETSTACK_OP_UDP_SEND
  ↓ (ARP cache miss)
Netstack work 1: NETSTACK_OP_ARP_LOOKUP
  ↓
VirtIO work 1: VIRTIO_OP_SEND_BUFFER (ARP request)
  → Complete, update cache
Netstack work 2: NETSTACK_OP_UDP_SEND (retry)
  ↓
VirtIO work 2: VIRTIO_OP_SEND_BUFFER (UDP packet)
  → Complete, propagate up
```

**Solution**: Child work stores parent work pointer:
```c
struct kwork {
    // ...
    kwork_t *parent;  // Original work from upper layer
    size_t pending_children;  // How many child ops outstanding
};

void layer_submit(kwork_t *parent_work) {
    // Create multiple child work items
    kwork_t *child1 = alloc_child_work(parent_work);
    kwork_t *child2 = alloc_child_work(parent_work);
    parent_work->pending_children = 2;

    machine_submit(sublayer, child1);
    machine_submit(sublayer, child2);
}

void child_complete_callback(kwork_t *child) {
    kwork_t *parent = child->parent;
    parent->pending_children--;

    if (parent->pending_children == 0) {
        // All children done - complete parent
        machine_complete(parent_machine, parent, KERR_OK);
    }
}
```

This works but adds state tracking complexity.

### Benefits of Unified API

**1. Conceptual Clarity**
- One pattern to learn and reason about
- "Everything is a state machine" is conceptually elegant
- Reduces cognitive load when understanding system

**2. Testability**
- Every layer has identical interface
- Mock any layer with same mechanism
- Inject test layers anywhere in stack

**3. Composability**
- Add new layers without changing existing ones
- Platform can dynamically choose sublayers
- Same application code works on different layer stacks

**4. Introspection and Debugging**
- Uniform work tracking across all layers
- Can trace work item through entire system
- Same debugging tools work everywhere

**5. Extensibility**
- Well-defined pattern for adding new operations
- Clear contract for new layers
- Easy to experiment with different implementations

### Costs of Unified API

**1. Memory Overhead**
- Child work items must be allocated
- Parent-child tracking structures
- More work items in flight simultaneously

**2. Complexity**
- Operation code translation between layers
- Parent-child completion tracking
- Error propagation across multiple layers

**3. Performance**
- Additional indirection for delegation
- More state machine transitions
- Overhead for synchronous operations

**4. Implementation Effort**
- Refactor existing platform.h interface
- Update all platform implementations
- Migration path for existing code

### Recommendations

After analyzing the trade-offs, we recommend **Approach 2: Hybrid API with Conceptual Uniformity**.

#### Keep Existing APIs

**User → Runtime**: Already uses work-based pattern (kwork_t + ksubmit)
```c
ksubmit(kernel, work);  // Keep as-is
```

**Runtime → Platform**: Keep direct function calls for efficiency
```c
platform_submit(platform, submissions, cancellations);  // Keep as-is
platform_tick(platform, kernel);
platform_wfi(platform, timeout);
```

**Platform → Subplatform**: Standardize on work-based pattern
```c
netstack_submit(netstack, work);  // Use consistent pattern
virtio_submit(virtio_dev, work);
```

#### Rationale

1. **User→Runtime boundary**: Work-based API is ideal here. User code benefits from async composability.

2. **Runtime→Platform boundary**: This is a well-defined, stable contract. Direct function calls are:
   - More efficient (no allocation overhead)
   - Simpler (clear ownership model)
   - Appropriate for a major architectural boundary
   - Platform.h already provides a clean abstraction

3. **Platform→Subplatform boundary**: This is where uniformity helps most:
   - Sublayers are platform-specific (not public API)
   - Variable depth stacks benefit from uniform interface
   - Easy to add/remove layers without changing runtime
   - Testability matters more than peak efficiency

#### Document the Pattern

Update documentation to emphasize the **abstract state machine pattern** that all layers follow:

```
All layers implement:
1. Accept work (requests from above)
2. Process work (execute or delegate)
3. Complete work (notify above)

Concrete APIs vary by boundary:
- User→Runtime: kwork_t + ksubmit() [work-based]
- Runtime→Platform: platform_submit() [batch function]
- Platform→Subplatform: sublayer_submit() [work-based]

Each uses the pattern appropriate for its needs.
```

#### Future Evolution

As the system matures, consider:

1. **Standardize sublayer interface**: Create common `sublayer_ops_t` for network stack, storage, etc.

2. **Optional unified mode**: Provide `platform_machine_t` wrapper that presents platform.h as a machine interface (for special use cases like testing or dynamic layer insertion).

3. **Operation registry**: Allow runtime registration of op code mappings (for extensibility without recompilation).

4. **Work pools**: Add per-layer work item allocators to reduce dynamic allocation overhead.

### Concrete Example: Network Stack Sublayer with Unified API

Here's how the network stack sublayer would implement the unified pattern:

```c
// Sublayer interface (common across all sublayers)
typedef struct {
    void (*submit)(void *layer_state, kwork_t *work);
    void (*tick)(void *layer_state);
    void (*complete)(void *layer_state, kwork_t *work, kerr_t result);
} sublayer_ops_t;

// Network stack sublayer
typedef struct {
    sublayer_ops_t ops;  // Implements sublayer interface

    // State
    arp_cache_t arp_cache;
    pending_work_t pending_arp[MAX_PENDING];

    // Configuration
    ip_addr_t local_ip;
    mac_addr_t local_mac;

    // Parent and child machines
    platform_t *platform;         // For completions
    sublayer_t *virtio_net;       // For delegation
} netstack_t;

// Submit implementation
void netstack_submit(void *state, kwork_t *work) {
    netstack_t *stack = (netstack_t *)state;

    // Operation code is in netstack op space
    switch (work->op) {
        case NETSTACK_OP_UDP_SEND: {
            knet_send_req_t *req = CONTAINER_OF(work, knet_send_req_t, work);

            // Parse destination
            ip_addr_t dest_ip = parse_dest_ip(req->packets[0].buffer);

            // ARP lookup
            mac_addr_t dest_mac;
            if (!arp_cache_lookup(&stack->arp_cache, dest_ip, &dest_mac)) {
                // Need ARP first - store work and initiate ARP
                pending_work_add(stack->pending_arp, dest_ip, work);

                // Create ARP work item (child work)
                kwork_t *arp_work = alloc_arp_work(work);  // Stores parent
                arp_work->op = NETSTACK_OP_ARP_LOOKUP;
                netstack_submit(stack, arp_work);  // Recursive
                return;
            }

            // Have MAC - build packet and delegate to VirtIO
            ethernet_frame_t frame;
            build_udp_packet(stack, req, dest_ip, dest_mac, &frame);

            // Translate to VirtIO op space
            kwork_t virtio_work;
            kwork_init(&virtio_work, VIRTIO_OP_SEND_BUFFER, &frame,
                       netstack_virtio_complete, 0);
            virtio_work.parent = work;  // Track parent

            stack->virtio_net->ops->submit(stack->virtio_net, &virtio_work);
            break;
        }

        case NETSTACK_OP_ARP_LOOKUP: {
            // Send ARP request via VirtIO
            // ...
            break;
        }
    }
}

// Completion callback when VirtIO finishes
void netstack_virtio_complete(kwork_t *virtio_work) {
    kwork_t *netstack_work = virtio_work->parent;

    // Propagate completion up to platform
    kplatform_complete_work(platform->kernel, netstack_work, KERR_OK);
}
```

Platform wires it up:
```c
void platform_submit(platform_t *p, kwork_t *submissions, kwork_t *cancellations) {
    for (kwork_t *w = submissions; w; w = w->next) {
        // Route by op code (in runtime op space)
        switch (w->op) {
            case KWORK_OP_NET_SEND:
                // Translate to netstack op space and delegate
                w->op = NETSTACK_OP_UDP_SEND;
                p->netstack.ops->submit(&p->netstack, w);
                break;

            case KWORK_OP_RNG_READ:
                // macOS handles directly (no sublayer needed)
                handle_rng_sync(p, w);
                break;
        }
    }
}
```

### Summary

The idea of using a unified state machine API across all layers is **conceptually appealing** and highlights the fundamental pattern underlying VMOS architecture. However, practical considerations suggest a **hybrid approach**:

1. **Recognize the pattern**: All layers follow submit→process→complete
2. **Use pattern appropriately**: Work-based API where it helps (User→Runtime, Platform→Subplatform), direct calls where it's cleaner (Runtime→Platform)
3. **Document uniformity**: Emphasize conceptual consistency even when concrete APIs differ
4. **Standardize sublayers**: Use uniform interface for sublayers (netstack, VirtIO, etc.)
5. **Keep flexibility**: Don't force uniformity where it adds complexity without benefit

The key insight is that **conceptual uniformity doesn't require API uniformity**. Each boundary can choose the most appropriate concrete API while following the same abstract pattern. This gives us the benefits of a clear mental model without the costs of unnecessary indirection.

## Platform Contract Implementation

### Platform.h Contract (27 Functions)

All platforms must implement these functions. For hosted platforms, the implementation strategy differs:

#### Section 1: Memory Management (1 function)

```c
kregions_t platform_mem_regions(platform_t *platform);
```

**Hosted strategy**: Allocate a memory pool with malloc() or mmap() and return it as a single region:
```c
// Allocate 16MB pool for kernel allocator
platform->mem_pool = malloc(16 * 1024 * 1024);
platform->mem_regions[0] = (kregion_t){
  .base = (uintptr_t)platform->mem_pool,
  .size = 16 * 1024 * 1024,
  .next = NULL,
  .prev = NULL
};
```

#### Section 2: PCI Configuration Space (7 functions)

```c
platform_pci_config_read8/16/32()
platform_pci_config_write8/16/32()
platform_pci_read_bar()
```

**Hosted strategy**: Return stubs - hosted platforms don't use PCI:
```c
uint32_t platform_pci_config_read32(...) { return 0xFFFFFFFF; }  // No device
void platform_pci_config_write32(...) { /* no-op */ }
uint64_t platform_pci_read_bar(...) { return 0; }
```

#### Section 2A: MMIO Access (7 functions)

```c
platform_mmio_read8/16/32/64()
platform_mmio_write8/16/32/64()
platform_mmio_barrier()
```

**Hosted strategy**: No MMIO - return stubs or direct memory access if needed:
```c
uint32_t platform_mmio_read32(volatile uint32_t *addr) { return *addr; }
void platform_mmio_write32(volatile uint32_t *addr, uint32_t val) { *addr = val; }
void platform_mmio_barrier(void) { __sync_synchronize(); }  // Or no-op
```

#### Section 3: Platform Lifecycle (2 functions)

```c
void platform_init(platform_t *platform, void *fdt, void *kernel);
ktime_t platform_wfi(platform_t *platform, ktime_t timeout_ns);
```

**Hosted strategy**:

**platform_init()**: Initialize kqueue, timers, and I/O subsystems:
```c
void platform_init(platform_t *platform, void *fdt, void *kernel) {
  memset(platform, 0, sizeof(platform_t));
  platform->kernel = kernel;

  // Create kqueue for all events
  platform->kqueue_fd = kqueue();
  if (platform->kqueue_fd < 0) {
    perror("kqueue");
    exit(1);
  }

  // Initialize time source
  mach_timebase_info(&platform->timebase_info);
  platform->time_start = mach_absolute_time();

  // Initialize I/O subsystems
  file_io_init(platform);
  network_init(platform);
  rng_init(platform);
}
```

**platform_wfi()**: **CRITICAL** - Maps to kevent() blocking call:
```c
ktime_t platform_wfi(platform_t *platform, ktime_t timeout_ns) {
  if (timeout_ns == 0) {
    return platform_get_time_ns(platform);
  }

  // Convert timeout to timespec
  struct timespec timeout;
  if (timeout_ns == UINT64_MAX) {
    timeout.tv_sec = 0;
    timeout.tv_nsec = 0;  // NULL in kevent() means infinite
  } else {
    timeout.tv_sec = timeout_ns / NSEC_PER_SEC;
    timeout.tv_nsec = timeout_ns % NSEC_PER_SEC;
  }

  // Wait for events
  struct kevent events[MAX_EVENTS];
  int nevents = kevent(platform->kqueue_fd,
                       NULL, 0,                // No changes to register
                       events, MAX_EVENTS,     // Retrieve events
                       timeout_ns == UINT64_MAX ? NULL : &timeout);

  if (nevents < 0) {
    if (errno == EINTR) {
      return platform_get_time_ns(platform);  // Signal interrupted
    }
    perror("kevent");
    exit(1);
  }

  // Process events and enqueue work completions
  for (int i = 0; i < nevents; i++) {
    struct kevent *ev = &events[i];

    switch (ev->filter) {
      case EVFILT_TIMER:
        handle_timer_event(platform, ev);
        break;
      case EVFILT_READ:
        handle_read_event(platform, ev);
        break;
      case EVFILT_WRITE:
        handle_write_event(platform, ev);
        break;
    }
  }

  return platform_get_time_ns(platform);
}
```

**Key insight**: platform_wfi() in bare-metal uses WFI/HLT to wait for hardware interrupts. In hosted, it uses kevent() to wait for OS events. Both implement the same semantic: "sleep until something happens or timeout expires."

#### Section 3A: Debug (2 functions)

```c
void platform_dump_registers(void);
void platform_dump_stack(uint32_t bytes);
```

**Hosted strategy**: Provide diagnostic output:
```c
void platform_dump_registers(void) {
  printf("=== Registers (macOS hosted, not available) ===\n");
}

void platform_dump_stack(uint32_t bytes) {
  printf("=== Stack Dump ===\n");
  void *buffer[64];
  int nptrs = backtrace(buffer, 64);
  backtrace_symbols_fd(buffer, nptrs, STDOUT_FILENO);
}
```

#### Section 4: Interrupt Control (2 functions)

```c
void platform_interrupt_enable(platform_t *platform);
void platform_interrupt_disable(platform_t *platform);
```

**Hosted strategy**: No-op - hosted platforms use single-threaded event loop:
```c
void platform_interrupt_enable(platform_t *platform) { /* no-op */ }
void platform_interrupt_disable(platform_t *platform) { /* no-op */ }
```

Alternative: Track enable/disable state and skip kevent() processing if disabled, but this is rarely needed.

#### Section 5: UART Debug (2 functions)

```c
void platform_uart_puts(const char *str);
void platform_uart_putc(char c);
```

**Hosted strategy**: Map to stdout:
```c
void platform_uart_puts(const char *str) {
  fputs(str, stdout);
  fflush(stdout);
}

void platform_uart_putc(char c) {
  putchar(c);
  fflush(stdout);
}
```

#### Section 7: IRQ Management (2 functions)

```c
int platform_irq_register(platform_t *platform, uint32_t irq_num,
                          void (*handler)(void *), void *context);
void platform_irq_enable(platform_t *platform, uint32_t irq_num);
```

**Hosted strategy**: Map IRQ numbers to event sources:
```c
// IRQ numbers in hosted platform map to event types
#define IRQ_TIMER     0
#define IRQ_FILE_READ 1
#define IRQ_FILE_WRITE 2
#define IRQ_NET_RX    3
#define IRQ_NET_TX    4
#define IRQ_RNG       5

typedef struct {
  void (*handler)(void *);
  void *context;
} irq_entry_t;

static irq_entry_t g_irq_table[32];

int platform_irq_register(platform_t *platform, uint32_t irq_num,
                          void (*handler)(void *), void *context) {
  if (irq_num >= 32) return -1;
  g_irq_table[irq_num].handler = handler;
  g_irq_table[irq_num].context = context;
  return 0;
}

void platform_irq_enable(platform_t *platform, uint32_t irq_num) {
  // Already enabled by registering with kqueue
}
```

#### Section 8: Work Submission (3 functions)

```c
void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations);
void platform_tick(platform_t *platform, kernel_t *k);
void platform_net_buffer_release(platform_t *platform, void *req,
                                  size_t buffer_index);
```

**Hosted strategy**: Route to device-specific handlers:

**platform_submit()**:
```c
void platform_submit(platform_t *platform, kwork_t *submissions,
                     kwork_t *cancellations) {
  // Process cancellations
  for (kwork_t *w = cancellations; w != NULL; w = w->next) {
    // Remove from kqueue if active
    cancel_work(platform, w);
  }

  // Route submissions by operation type
  for (kwork_t *w = submissions; w != NULL; w = w->next) {
    switch (w->op) {
      case KWORK_OP_TIMER:
        submit_timer(platform, CONTAINER_OF(w, ktimer_req_t, work));
        break;
      case KWORK_OP_RNG_READ:
        submit_rng(platform, CONTAINER_OF(w, krng_req_t, work));
        break;
      case KWORK_OP_BLOCK_READ:
      case KWORK_OP_BLOCK_WRITE:
      case KWORK_OP_BLOCK_FLUSH:
        submit_file_io(platform, CONTAINER_OF(w, kblk_req_t, work));
        break;
      case KWORK_OP_NET_RECV:
        submit_net_recv(platform, CONTAINER_OF(w, knet_recv_req_t, work));
        break;
      case KWORK_OP_NET_SEND:
        submit_net_send(platform, CONTAINER_OF(w, knet_send_req_t, work));
        break;
    }
  }
}
```

**platform_tick()**:
```c
void platform_tick(platform_t *platform, kernel_t *k) {
  // In bare-metal: process IRQ ring (deferred interrupt processing)
  // In hosted: no deferred processing needed, events already handled
  // This can be no-op or do bookkeeping
}
```

**platform_net_buffer_release()**:
```c
void platform_net_buffer_release(platform_t *platform, void *req,
                                  size_t buffer_index) {
  knet_recv_req_t *recv_req = (knet_recv_req_t *)req;
  // Re-register buffer with kqueue for next receive
  resubmit_net_buffer(platform, recv_req, buffer_index);
}
```

## Device Implementation Strategy

### Timer Events

**API Surface**: ktimer_req_t with deadline_ns

**Implementation**: Use kqueue EVFILT_TIMER
```c
void submit_timer(platform_t *platform, ktimer_req_t *timer) {
  // Timers managed by kernel (min-heap), but platform can set a wakeup timer
  // In hosted: register kqueue timer for deadline

  uint64_t now_ns = platform_get_time_ns(platform);
  int64_t delay_ns = timer->deadline_ns - now_ns;
  if (delay_ns < 0) delay_ns = 0;

  // Convert to milliseconds for kqueue
  int64_t delay_ms = delay_ns / 1000000;

  struct kevent kev;
  EV_SET(&kev, (uintptr_t)timer, EVFILT_TIMER, EV_ADD | EV_ONESHOT,
         NOTE_USECONDS, delay_ms * 1000, timer);

  if (kevent(platform->kqueue_fd, &kev, 1, NULL, 0, NULL) < 0) {
    perror("kevent timer");
  }

  timer->work.state = KWORK_STATE_LIVE;
}

void handle_timer_event(platform_t *platform, struct kevent *ev) {
  ktimer_req_t *timer = (ktimer_req_t *)ev->udata;
  kernel_t *k = (kernel_t *)platform->kernel;
  kplatform_complete_work(k, &timer->work, KERR_OK);
}
```

**Note**: The kernel already manages a timer heap. The platform just needs to wake up when the earliest timer expires. This can be simplified to set one kqueue timer for the next deadline.

### Random Number Generation (RNG)

**API Surface**: krng_req_t with buffer and length

**Implementation Options**:

**Option 1: Synchronous (arc4random_buf)**
```c
void submit_rng(platform_t *platform, krng_req_t *req) {
  // Immediately fill buffer
  arc4random_buf(req->buffer, req->length);
  req->completed = req->length;

  // Complete immediately
  kernel_t *k = (kernel_t *)platform->kernel;
  req->work.state = KWORK_STATE_LIVE;
  kplatform_complete_work(k, &req->work, KERR_OK);
}
```

**Option 2: Asynchronous (/dev/urandom + kqueue)**
```c
void submit_rng(platform_t *platform, krng_req_t *req) {
  // Open /dev/urandom if not already open
  if (platform->urandom_fd < 0) {
    platform->urandom_fd = open("/dev/urandom", O_RDONLY | O_NONBLOCK);
  }

  // Register for read events
  struct kevent kev;
  EV_SET(&kev, platform->urandom_fd, EVFILT_READ, EV_ADD | EV_ONESHOT,
         0, 0, req);
  kevent(platform->kqueue_fd, &kev, 1, NULL, 0, NULL);

  req->work.state = KWORK_STATE_LIVE;
}

void handle_rng_read_event(platform_t *platform, struct kevent *ev) {
  krng_req_t *req = (krng_req_t *)ev->udata;

  ssize_t n = read(platform->urandom_fd, req->buffer, req->length);
  if (n < 0) {
    req->completed = 0;
    kplatform_complete_work((kernel_t *)platform->kernel,
                           &req->work, KERR_IO_ERROR);
  } else {
    req->completed = n;
    kplatform_complete_work((kernel_t *)platform->kernel,
                           &req->work, KERR_OK);
  }
}
```

**Recommendation**: Use Option 1 (synchronous) for simplicity. RNG is fast and non-blocking.

### File I/O (Block Device Replacement)

**API Surface**:
- kblk_req_t with segments[] for scatter-gather
- KWORK_OP_BLOCK_READ, KWORK_OP_BLOCK_WRITE, KWORK_OP_BLOCK_FLUSH

**Implementation**: POSIX file operations + kqueue EVFILT_READ/WRITE

**Data Structure**:
```c
// In platform_impl.h
typedef struct {
  int fd;                // File descriptor
  off_t offset;          // Current segment offset
  size_t segment_idx;    // Which segment we're on
} kblk_req_platform_t;

struct platform {
  // ...
  int block_device_fd;   // Open file descriptor for block device
  char block_device_path[256];  // Path to file
};
```

**Initialization**:
```c
void file_io_init(platform_t *platform) {
  // Open a file to act as block device
  const char *path = getenv("VMOS_BLOCK_DEVICE");
  if (!path) path = "./vmos_disk.img";

  platform->block_device_fd = open(path, O_RDWR | O_CREAT | O_NONBLOCK, 0644);
  if (platform->block_device_fd < 0) {
    perror("open block device");
    exit(1);
  }

  strncpy(platform->block_device_path, path, sizeof(platform->block_device_path));
}
```

**Submission**:
```c
void submit_file_io(platform_t *platform, kblk_req_t *req) {
  // For simplicity: only handle single-segment initially
  kblk_segment_t *seg = &req->segments[0];

  req->platform.fd = platform->block_device_fd;
  req->platform.offset = seg->sector * 512;  // Assume 512-byte sectors
  req->platform.segment_idx = 0;

  // Register with kqueue
  struct kevent kev;
  int filter = (req->work.op == KWORK_OP_BLOCK_READ) ? EVFILT_READ : EVFILT_WRITE;
  EV_SET(&kev, platform->block_device_fd, filter, EV_ADD | EV_ONESHOT,
         0, 0, req);
  kevent(platform->kqueue_fd, &kev, 1, NULL, 0, NULL);

  req->work.state = KWORK_STATE_LIVE;

  // Initiate I/O
  if (req->work.op == KWORK_OP_BLOCK_READ) {
    ssize_t n = pread(req->platform.fd, seg->buffer,
                      seg->num_sectors * 512, req->platform.offset);
    if (n >= 0) {
      seg->completed_sectors = n / 512;
    }
  } else if (req->work.op == KWORK_OP_BLOCK_WRITE) {
    ssize_t n = pwrite(req->platform.fd, seg->buffer,
                       seg->num_sectors * 512, req->platform.offset);
    if (n >= 0) {
      seg->completed_sectors = n / 512;
    }
  }
}

void handle_file_io_event(platform_t *platform, struct kevent *ev) {
  kblk_req_t *req = (kblk_req_t *)ev->udata;

  // Check if I/O completed
  if (ev->flags & EV_ERROR) {
    kplatform_complete_work((kernel_t *)platform->kernel,
                           &req->work, KERR_IO_ERROR);
    return;
  }

  // Mark as complete
  kplatform_complete_work((kernel_t *)platform->kernel,
                         &req->work, KERR_OK);
}
```

**Note**: File I/O on macOS is often synchronous even with O_NONBLOCK. Consider using a thread pool for truly async I/O, or accept that it may block briefly.

**Flush**:
```c
void submit_file_flush(platform_t *platform, kblk_req_t *req) {
  if (fsync(platform->block_device_fd) < 0) {
    kplatform_complete_work((kernel_t *)platform->kernel,
                           &req->work, KERR_IO_ERROR);
  } else {
    kplatform_complete_work((kernel_t *)platform->kernel,
                           &req->work, KERR_OK);
  }
}
```

### Network I/O (UDP/IP)

**API Surface**:
- knet_recv_req_t with ring of buffers (standing work)
- knet_send_req_t with array of packets
- KWORK_OP_NET_RECV, KWORK_OP_NET_SEND

**Implementation**: BSD sockets + kqueue EVFILT_READ/WRITE

**Data Structure**:
```c
// In platform_impl.h
typedef struct {
  int socket_fd;         // Socket for this receive
  size_t buffer_idx;     // Which buffer in ring
} knet_recv_req_platform_t;

typedef struct {
  int socket_fd;         // Socket for this send
  size_t packets_sent;   // How many packets sent so far
} knet_send_req_platform_t;

struct platform {
  // ...
  int udp_socket_fd;     // UDP socket
  struct sockaddr_in local_addr;
};
```

**Initialization**:
```c
void network_init(platform_t *platform) {
  // Create UDP socket
  platform->udp_socket_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (platform->udp_socket_fd < 0) {
    perror("socket");
    exit(1);
  }

  // Bind to a port (default: random port, or from env)
  platform->local_addr.sin_family = AF_INET;
  platform->local_addr.sin_addr.s_addr = INADDR_ANY;
  platform->local_addr.sin_port = htons(0);  // Ephemeral port

  if (bind(platform->udp_socket_fd,
           (struct sockaddr *)&platform->local_addr,
           sizeof(platform->local_addr)) < 0) {
    perror("bind");
    exit(1);
  }

  // Get actual bound port
  socklen_t len = sizeof(platform->local_addr);
  getsockname(platform->udp_socket_fd,
              (struct sockaddr *)&platform->local_addr, &len);

  printf("UDP socket bound to port %d\n", ntohs(platform->local_addr.sin_port));
}
```

**Receive (Standing Work)**:
```c
void submit_net_recv(platform_t *platform, knet_recv_req_t *req) {
  // Standing work: register all buffers for receive
  req->platform.socket_fd = platform->udp_socket_fd;

  // Register socket for read events
  struct kevent kev;
  EV_SET(&kev, platform->udp_socket_fd, EVFILT_READ, EV_ADD,
         0, 0, req);
  kevent(platform->kqueue_fd, &kev, 1, NULL, 0, NULL);

  req->work.state = KWORK_STATE_LIVE;
}

void handle_net_recv_event(platform_t *platform, struct kevent *ev) {
  knet_recv_req_t *req = (knet_recv_req_t *)ev->udata;

  // Find next available buffer (simple: round-robin)
  size_t idx = req->platform.buffer_idx;
  knet_buffer_t *buf = &req->buffers[idx];

  // Receive packet
  struct sockaddr_in src_addr;
  socklen_t src_len = sizeof(src_addr);
  ssize_t n = recvfrom(req->platform.socket_fd, buf->buffer, buf->buffer_size,
                       0, (struct sockaddr *)&src_addr, &src_len);

  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return;  // No data yet
    }
    kplatform_complete_work((kernel_t *)platform->kernel,
                           &req->work, KERR_IO_ERROR);
    return;
  }

  buf->packet_length = n;
  req->buffer_index = idx;

  // Advance to next buffer
  req->platform.buffer_idx = (idx + 1) % req->num_buffers;

  // Complete work (standing work stays LIVE)
  kplatform_complete_work((kernel_t *)platform->kernel,
                         &req->work, KERR_OK);
}
```

**Send**:
```c
void submit_net_send(platform_t *platform, knet_send_req_t *req) {
  req->platform.socket_fd = platform->udp_socket_fd;
  req->platform.packets_sent = 0;

  // Try to send all packets immediately
  for (size_t i = 0; i < req->num_packets; i++) {
    knet_buffer_t *pkt = &req->packets[i];

    // For UDP, need destination address (assume stored in first bytes?)
    // Or extend API to include sockaddr
    struct sockaddr_in dest_addr;
    // ... parse destination from packet or extend API ...

    ssize_t n = sendto(platform->udp_socket_fd, pkt->buffer, pkt->buffer_size,
                       0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Register for write events and retry later
        struct kevent kev;
        EV_SET(&kev, platform->udp_socket_fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT,
               0, 0, req);
        kevent(platform->kqueue_fd, &kev, 1, NULL, 0, NULL);
        req->work.state = KWORK_STATE_LIVE;
        return;
      } else {
        // Error
        req->packets_sent = i;
        kplatform_complete_work((kernel_t *)platform->kernel,
                               &req->work, KERR_IO_ERROR);
        return;
      }
    }

    req->platform.packets_sent++;
  }

  // All sent
  req->packets_sent = req->num_packets;
  req->work.state = KWORK_STATE_LIVE;
  kplatform_complete_work((kernel_t *)platform->kernel,
                         &req->work, KERR_OK);
}
```

**API Extension for Network**: The current API doesn't include destination addresses. Recommendations:
1. Store destination in first bytes of buffer (application-defined format)
2. Extend knet_send_req_t with sockaddr field
3. Use environment variable for default destination

## Build System Integration

### Makefile Structure

Add to root Makefile:
```makefile
ifeq ($(PLATFORM), macos)
  include platform/macos/platform.mk
endif
```

### platform/macos/platform.mk

```makefile
# macOS hosted platform configuration

TARGET = x86_64-apple-macosx11.0.0

# No QEMU for hosted platforms
QEMU =
QEMU_MACHINE =
QEMU_CPU =
QEMU_EXTRA_ARGS =

# Platform-specific sources
PLATFORM_C_SRCS = platform_init.c platform_wfi.c kqueue_events.c \
                  timer.c file_io.c network.c rng.c debug.c mem.c

PLATFORM_S_SRCS =  # No assembly needed

# Shared sources we DON'T use (VirtIO not needed)
# PLATFORM_SHARED_SRCS intentionally empty or minimal

# Compiler flags
PLATFORM_CFLAGS = -std=c11 -D_DARWIN_C_SOURCE
PLATFORM_LDFLAGS = -framework CoreFoundation

# Override kernel output: produce executable instead of ELF
KERNEL = vmos_macos
```

### Running

For bare-metal:
```bash
make run PLATFORM=arm64
```

For hosted macOS:
```bash
make PLATFORM=macos
./build/macos/vmos_macos
```

No QEMU required - runs as native process.

## Implementation Roadmap

### Phase 1: Minimal Viable Platform (Timer + Event Loop)
**Goal**: Get kmain() event loop running with timer support

1. Create platform/macos/ directory structure
2. Implement platform_impl.h with basic platform_t
3. Implement platform_init() with kqueue setup
4. Implement timer.c with mach_absolute_time()
5. Implement platform_wfi() with kevent()
6. Implement stub functions (PCI, MMIO, IRQ, debug)
7. Update Makefile
8. Test: Run kernel and verify timer callbacks fire

**Test program**:
```c
void user_main(user_t *user) {
  ktimer_req_t timer;
  kwork_init(&timer.work, KWORK_OP_TIMER, NULL, timer_callback, 0);
  timer.deadline_ns = user->kernel->current_time_ns + SEC_TO_NS(1);
  ksubmit(user->kernel, &timer.work);
}

void timer_callback(kwork_t *work) {
  printk("Timer fired!\n");
}
```

### Phase 2: RNG Support
**Goal**: Add random number generation

1. Implement rng.c with arc4random_buf()
2. Implement submit_rng() in platform_submit()
3. Test: Request random bytes and verify

**Test program**:
```c
void user_main(user_t *user) {
  uint8_t buffer[32];
  krng_req_t rng;
  kwork_init(&rng.work, KWORK_OP_RNG_READ, NULL, rng_callback, 0);
  rng.buffer = buffer;
  rng.length = 32;
  ksubmit(user->kernel, &rng.work);
}

void rng_callback(kwork_t *work) {
  krng_req_t *req = CONTAINER_OF(work, krng_req_t, work);
  printk("Got %zu random bytes\n", req->completed);
}
```

### Phase 3: File I/O
**Goal**: Add block device operations

1. Implement file_io.c with POSIX open/read/write/fsync
2. Implement submit_file_io() for read/write/flush
3. Handle kqueue events for file I/O
4. Test: Read/write blocks to file

**Test program**:
```c
void user_main(user_t *user) {
  uint8_t buffer[4096];
  kblk_segment_t seg = { .sector = 0, .buffer = buffer, .num_sectors = 8 };

  kblk_req_t req;
  kwork_init(&req.work, KWORK_OP_BLOCK_WRITE, NULL, write_callback, 0);
  req.segments = &seg;
  req.num_segments = 1;
  ksubmit(user->kernel, &req.work);
}
```

### Phase 4: Network I/O (UDP)
**Goal**: Add network send/receive

1. Implement network.c with BSD sockets
2. Implement submit_net_recv() and submit_net_send()
3. Handle standing work for receive
4. Test: Send and receive UDP packets

**Test program**:
```c
void user_main(user_t *user) {
  knet_buffer_t buffers[4];
  for (int i = 0; i < 4; i++) {
    buffers[i].buffer = malloc(1500);
    buffers[i].buffer_size = 1500;
  }

  knet_recv_req_t recv;
  kwork_init(&recv.work, KWORK_OP_NET_RECV, NULL, recv_callback,
             KWORK_FLAG_STANDING);
  recv.buffers = buffers;
  recv.num_buffers = 4;
  ksubmit(user->kernel, &recv.work);
}

void recv_callback(kwork_t *work) {
  knet_recv_req_t *req = CONTAINER_OF(work, knet_recv_req_t, work);
  knet_buffer_t *buf = &req->buffers[req->buffer_index];
  printk("Received %zu bytes\n", buf->packet_length);
}
```

## Key Design Decisions

### Single-Threaded Event Loop
**Decision**: Use single-threaded kqueue event loop, no background threads

**Rationale**: Matches bare-metal model (single CPU core), avoids locking complexity

**Trade-off**: File I/O may block briefly, but acceptable for hosted environment

### Synchronous RNG
**Decision**: Use arc4random_buf() synchronously instead of async /dev/urandom

**Rationale**: RNG is fast, non-blocking, and simplifies implementation

**Alternative**: Could use async for consistency, but adds complexity

### File-Backed Block Device
**Decision**: Use regular file for block device, not raw disk

**Rationale**: Safe, portable, easy to create/delete

**Alternative**: Could support raw devices with elevated privileges

### UDP-Only Network
**Decision**: Start with UDP sockets, not TCP

**Rationale**: UDP is simpler (no connection state), matches VirtIO packet model

**Future**: Could add TCP support with connection management

### No VirtIO Layer
**Decision**: Don't use VirtIO structures for hosted platform

**Rationale**: VirtIO is designed for VM device interfaces. Hosted platform talks directly to OS.

**Consequence**: Can't share platform/shared/platform_virtio.c, but this is acceptable

## Platform Data Structures

### platform_impl.h
```c
#pragma once

#include <sys/event.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <mach/mach_time.h>

typedef struct platform {
  // Kernel back-pointer
  void *kernel;

  // kqueue for all events
  int kqueue_fd;

  // Time source
  mach_timebase_info_data_t timebase_info;
  uint64_t time_start;  // mach_absolute_time() at init

  // Memory management
  void *mem_pool;
  kregion_t mem_regions[4];
  kregion_t *mem_regions_head;
  kregion_t *mem_regions_tail;

  // File I/O (block device)
  int block_device_fd;
  char block_device_path[256];

  // Network
  int udp_socket_fd;
  struct sockaddr_in local_addr;

  // RNG
  int urandom_fd;  // -1 if using arc4random_buf

} platform_t;

// Platform-specific work request fields
typedef struct {
  int fd;
  off_t offset;
  size_t segment_idx;
} kblk_req_platform_t;

typedef struct {
  // RNG is synchronous, no platform state needed
} krng_req_platform_t;

typedef struct {
  int socket_fd;
  size_t buffer_idx;
} knet_recv_req_platform_t;

typedef struct {
  int socket_fd;
  size_t packets_sent;
} knet_send_req_platform_t;

// Inline MMIO functions (no-op for hosted)
static inline uint64_t platform_mmio_read64(volatile uint64_t *addr) {
  return *addr;
}

static inline void platform_mmio_write64(volatile uint64_t *addr, uint64_t val) {
  *addr = val;
}

static inline void platform_mmio_barrier(void) {
  __sync_synchronize();  // Or no-op
}
```

## API Extensions and Considerations

### Network Address Specification
Current API doesn't include destination addresses for send operations. Options:

**Option A**: Extend API with address field
```c
typedef struct {
  kwork_t work;
  knet_buffer_t *packets;
  size_t num_packets;
  size_t packets_sent;
  struct sockaddr_storage dest_addr;  // NEW
  socklen_t dest_addr_len;            // NEW
  knet_send_req_platform_t platform;
} knet_send_req_t;
```

**Option B**: Store address in buffer header
```c
// Application defines packet format:
struct udp_packet {
  struct sockaddr_in dest;
  uint8_t data[1400];
};
```

**Option C**: Use environment or config file for default destination
```bash
export VMOS_UDP_DEST=127.0.0.1:8080
./vmos_macos
```

**Recommendation**: Option C for initial implementation, Option A for production

### Multiple Block Devices
Current design assumes single file. For multiple files:
```c
struct platform {
  // ...
  int block_device_fds[8];
  char block_device_paths[8][256];
  int num_block_devices;
};
```

Extend API with device ID or use file path in request.

## Testing and Validation

### Unit Tests
1. **Timer test**: Schedule multiple timers, verify order and timing
2. **RNG test**: Request random bytes, verify length and randomness
3. **File I/O test**: Write data, read back, verify contents
4. **Network test**: Send packet to self, verify receive

### Integration Tests
1. **Work queue stress**: Submit many work items, verify all complete
2. **Standing work**: Verify network receive stays alive after callback
3. **Cancellation**: Cancel pending work, verify it stops
4. **Error handling**: Trigger I/O errors, verify error codes

### Comparison with Bare-Metal
Run same user application on both:
```bash
make test PLATFORM=arm64    # QEMU
make test PLATFORM=macos     # Hosted
```

Verify identical behavior at API level.

## Future Enhancements

### Other Hosted Platforms
- **Linux**: Use epoll instead of kqueue
- **Windows**: Use IOCP (I/O Completion Ports)
- **FreeBSD/OpenBSD**: Use kqueue (similar to macOS)

### Advanced Features
- **TCP support**: Connection-oriented streams
- **Multiple files**: Per-file descriptors
- **Threading**: Optional thread pool for blocking I/O
- **Shared memory**: For inter-process communication
- **Signal handling**: Graceful shutdown

### Performance Optimization
- **io_uring** (Linux): High-performance async I/O
- **Batched I/O**: Multiple operations per kevent() call
- **Zero-copy**: splice(), sendfile() for large transfers

## Summary

The hosted platform architecture demonstrates VMOS's portability by running the same event loop and work queue system on macOS as a user-space process. Key points:

1. **Zero changes** to kernel/kmain.c, kernel/kernel.c, or kapi.h
2. **27 functions** in platform.h implemented using macOS APIs
3. **kqueue replaces hardware interrupts** as the central event dispatch mechanism
4. **Native I/O replaces VirtIO**: files replace block device, sockets replace network device
5. **Same user API**: applications work identically on bare-metal and hosted

This validates the platform abstraction and enables:
- Rapid development without QEMU
- Native debugging with lldb/Instruments
- Easy integration testing
- Proof that event loop is truly portable

The macOS platform serves as a reference for other hosted platforms (Linux, Windows, BSD).

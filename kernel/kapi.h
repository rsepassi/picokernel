// vmos Kernel API
// User-facing kernel API for async work submission

#pragma once

#include "platform.h"

// Forward declarations
typedef struct kwork kwork_t;
typedef struct kernel kernel_t;

// Error codes
typedef uint32_t kerr_t;

#define KERR_OK 0        // Success
#define KERR_BUSY 1      // Resource busy
#define KERR_INVALID 2   // Invalid argument
#define KERR_CANCELLED 3 // Operation cancelled
#define KERR_TIMEOUT 4   // Timeout
#define KERR_NO_DEVICE 5 // Device not available
#define KERR_IO_ERROR 6  // I/O error (bad sector, transmission failure)
#define KERR_NO_SPACE 7  // Device full (block) or queue full (network)

// Work item states
typedef enum {
  KWORK_STATE_DEAD = 0,             // Not initialized
  KWORK_STATE_SUBMIT_REQUESTED = 1, // Queued for submission
  KWORK_STATE_LIVE = 2,             // Submitted to platform/timer
  KWORK_STATE_READY = 3,            // Ready for callback
  KWORK_STATE_CANCEL_REQUESTED = 4, // Cancel requested
} kwork_state_t;

// Work item flags (for kwork_t.flags field)
#define KWORK_FLAG_STANDING 0x01 // Work remains LIVE after completion

// Operation types
typedef enum {
  KWORK_OP_TIMER = 1,       // Timer expiration
  KWORK_OP_RNG_READ = 2,    // Random number generation
  KWORK_OP_BLOCK_READ = 3,  // Block device read
  KWORK_OP_BLOCK_WRITE = 4, // Block device write
  KWORK_OP_BLOCK_FLUSH = 5, // Block device flush/fsync
  KWORK_OP_NET_RECV = 6,    // Network packet receive
  KWORK_OP_NET_SEND = 7,    // Network packet send
} kwork_op_t;

// Work item callback
typedef void (*kwork_callback_t)(kwork_t *work);

// Core work item structure
struct kwork {
  uint32_t op;               // Operation type (kwork_op_t)
  kwork_callback_t callback; // Completion callback
  void *ctx;                 // Caller context
  kerr_t result;             // Result code
  uint8_t state;             // kwork_state_t
  uint8_t flags;             // KWORK_FLAG_* bits
  kwork_t *next;             // Intrusive list
  kwork_t *prev;             // Intrusive list (submit/cancel queues only)
};

// Timer request structure
typedef struct ktimer_req ktimer_req_t;
struct ktimer_req {
  kwork_t work;              // Embedded work item
  uint64_t deadline_ms;      // Absolute deadline
  ktimer_req_t *parent;      // Intrusive heap parent
  ktimer_req_t *left;        // Intrusive heap left child
  ktimer_req_t *right;       // Intrusive heap right child
};

// RNG request structure
typedef struct {
  kwork_t work;                 // Embedded work item
  uint8_t *buffer;              // Buffer to fill
  size_t length;                // Bytes requested
  size_t completed;             // Bytes actually read
  krng_req_platform_t platform; // Platform-specific fields
} krng_req_t;

// Block device structures

// I/O segment for scatter-gather operations
typedef struct {
  uint64_t sector;          // Starting sector number
  uint8_t *buffer;          // Data buffer (must be 4K-aligned)
  size_t num_sectors;       // Number of sectors to transfer
  size_t completed_sectors; // Sectors actually transferred (for partial I/O)
} kblk_segment_t;

// Block request structure
typedef struct {
  kwork_t work;                 // Embedded work item
  kblk_segment_t *segments;     // Array of I/O segments
  size_t num_segments;          // Number of segments
  kblk_req_platform_t platform; // Platform-specific fields
} kblk_req_t;

// Network device structures

// Network buffer for send/receive operations
typedef struct {
  uint8_t *buffer;      // Packet buffer
  size_t buffer_size;   // Buffer capacity (recv) or packet size (send)
  size_t packet_length; // Actual packet length (filled on completion)
} knet_buffer_t;

// Network receive request structure (ring buffer semantics)
typedef struct {
  kwork_t work;           // Embedded work item
  knet_buffer_t *buffers; // Array of receive buffers (ring)
  size_t num_buffers;     // Number of buffers in ring
  size_t buffer_index;    // Which buffer was filled (set on completion)
  knet_recv_req_platform_t platform; // Platform-specific fields
} knet_recv_req_t;

// Network send request structure
typedef struct {
  kwork_t work;                      // Embedded work item
  knet_buffer_t *packets;            // Array of packets to send
  size_t num_packets;                // Number of packets
  size_t packets_sent;               // Number of packets actually sent
  knet_send_req_platform_t platform; // Platform-specific fields
} knet_send_req_t;

// CONTAINER_OF macro to recover containing structure from work pointer
#define CONTAINER_OF(ptr, type, member)                                        \
  ((type *)((void *)((uint8_t *)(ptr) - offsetof(type, member))))

// Kernel API Functions

// Submit work item (queues for bulk submission)
kerr_t ksubmit(kernel_t *k, kwork_t *work);

// Request cancellation (best-effort)
kerr_t kcancel(kernel_t *k, kwork_t *work);

// Initialize work item
void kwork_init(kwork_t *work, uint32_t op, void *ctx,
                kwork_callback_t callback, uint8_t flags);

// Release a network receive buffer back to the ring
void knet_buffer_release(kernel_t *k, knet_recv_req_t *req,
                         size_t buffer_index);

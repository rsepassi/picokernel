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

// Work item states
typedef enum {
  KWORK_STATE_DEAD = 0,             // Not initialized
  KWORK_STATE_SUBMIT_REQUESTED = 1, // Queued for submission
  KWORK_STATE_LIVE = 2,             // Submitted to platform/timer
  KWORK_STATE_READY = 3,            // Ready for callback
  KWORK_STATE_CANCEL_REQUESTED = 4, // Cancel requested
} kwork_state_t;

// Operation types
typedef enum {
  KWORK_OP_TIMER = 1,    // Timer expiration
  KWORK_OP_RNG_READ = 2, // Random number generation
                         // Future: KWORK_OP_BLOCK_READ, KWORK_OP_NET_SEND, etc.
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
  uint8_t flags;             // Reserved for future use
  kwork_t *next;             // Intrusive list
  kwork_t *prev;             // Intrusive list (submit/cancel queues only)
};

// Timer request structure
typedef struct {
  kwork_t work;         // Embedded work item
  uint64_t deadline_ms; // Absolute deadline
} ktimer_req_t;

// RNG request structure
typedef struct {
  kwork_t work;                 // Embedded work item
  uint8_t *buffer;              // Buffer to fill
  size_t length;                // Bytes requested
  size_t completed;             // Bytes actually read
  krng_req_platform_t platform; // Platform-specific fields
} krng_req_t;

// CONTAINER_OF macro to recover containing structure from work pointer
#define CONTAINER_OF(ptr, type, member)                                        \
  ((type *)((void *)((uint8_t *)(ptr) - offsetof(type, member))))

// Kernel API Functions

// Submit work item (queues for bulk submission)
kerr_t ksubmit(kernel_t *k, kwork_t *work);

// Request cancellation (best-effort)
kerr_t kcancel(kernel_t *k, kwork_t *work);

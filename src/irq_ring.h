#ifndef IRQ_RING_H
#define IRQ_RING_H

#include <stdbool.h>
#include <stdint.h>

// Ring buffer size (power of 2 recommended for efficient modulo)
#define KIRQ_RING_SIZE 256

// IRQ ring buffer structure (opaque to users)
typedef struct kirq_ring kirq_ring_t;

// Initialize the ring buffer
// Must be called before any other operations
void kirq_ring_init(kirq_ring_t *ring);

// Enqueue a device pointer (called from ISR)
// Returns: true on success, false if ring is full (overflow)
// On overflow, the overflow counter is incremented automatically
bool kirq_ring_enqueue(kirq_ring_t *ring, void *device);

// Dequeue a device pointer (called from platform_tick)
// Returns: device pointer, or NULL if ring is empty
void *kirq_ring_dequeue(kirq_ring_t *ring);

// Snapshot the current write position (for bounded dequeue)
// Use this to capture the end position before starting a dequeue loop
uint32_t kirq_ring_snapshot(const kirq_ring_t *ring);

// Dequeue a device pointer up to a captured end position
// Returns: device pointer, or NULL if ring is empty or end_pos reached
// Use with kirq_ring_snapshot() to avoid infinite loops when devices re-enqueue
void *kirq_ring_dequeue_bounded(kirq_ring_t *ring, uint32_t end_pos);

// Check if ring is empty (read_pos == write_pos)
bool kirq_ring_is_empty(const kirq_ring_t *ring);

// Get overflow counter (number of dropped interrupts)
uint32_t kirq_ring_overflow_count(const kirq_ring_t *ring);

// Select implementation: 0 = volatile+barriers, 1 = C11 atomics
#ifndef KIRQ_RING_USE_ATOMICS
#define KIRQ_RING_USE_ATOMICS 0
#endif

#if KIRQ_RING_USE_ATOMICS
#include <stdatomic.h>
struct kirq_ring {
  void *items[KIRQ_RING_SIZE];
  _Atomic uint32_t write_pos;      // ISR writes (producer)
  _Atomic uint32_t read_pos;       // platform_tick reads (consumer)
  _Atomic uint32_t overflow_count; // Dropped interrupt counter
};
#else
struct kirq_ring {
  void *items[KIRQ_RING_SIZE];
  volatile uint32_t write_pos;      // ISR writes (producer)
  volatile uint32_t read_pos;       // platform_tick reads (consumer)
  volatile uint32_t overflow_count; // Dropped interrupt counter
};
#endif

#endif // IRQ_RING_H

#include "irq_ring.h"
#include <stddef.h>

void kirq_ring_init(kirq_ring_t *ring) {
  ring->write_pos = 0;
  ring->read_pos = 0;
  ring->overflow_count = 0;
}

bool kirq_ring_enqueue(kirq_ring_t *ring, void *device) {
  uint32_t write_pos =
      atomic_load_explicit(&ring->write_pos, memory_order_relaxed);
  uint32_t read_pos =
      atomic_load_explicit(&ring->read_pos, memory_order_acquire);
  uint32_t next_write = write_pos + 1;

  // Check for overflow
  if (next_write - read_pos >= KIRQ_RING_SIZE) {
    atomic_fetch_add_explicit(&ring->overflow_count, 1, memory_order_relaxed);
    return false;
  }

  // Store device pointer
  ring->items[write_pos % KIRQ_RING_SIZE] = device;

  // Publish write (release semantics ensures item is visible)
  atomic_store_explicit(&ring->write_pos, next_write, memory_order_release);
  return true;
}

void *kirq_ring_dequeue(kirq_ring_t *ring) {
  uint32_t read_pos =
      atomic_load_explicit(&ring->read_pos, memory_order_relaxed);
  uint32_t write_pos =
      atomic_load_explicit(&ring->write_pos, memory_order_acquire);

  // Check if empty
  if (read_pos == write_pos) {
    return NULL;
  }

  // Load device pointer (acquire ensures it's visible)
  void *device = ring->items[read_pos % KIRQ_RING_SIZE];

  // Advance read position
  atomic_store_explicit(&ring->read_pos, read_pos + 1, memory_order_release);
  return device;
}

bool kirq_ring_is_empty(const kirq_ring_t *ring) {
  uint32_t read_pos =
      atomic_load_explicit(&ring->read_pos, memory_order_relaxed);
  uint32_t write_pos =
      atomic_load_explicit(&ring->write_pos, memory_order_acquire);
  return read_pos == write_pos;
}

uint32_t kirq_ring_snapshot(const kirq_ring_t *ring) {
  // Acquire ordering ensures we see all enqueued items up to this point
  return atomic_load_explicit(&ring->write_pos, memory_order_acquire);
}

void *kirq_ring_dequeue_bounded(kirq_ring_t *ring, uint32_t end_pos) {
  uint32_t read_pos =
      atomic_load_explicit(&ring->read_pos, memory_order_relaxed);

  // Check if we've reached the end position
  if (read_pos == end_pos) {
    return NULL;
  }

  // Also check if ring is empty (write_pos may have been captured earlier)
  uint32_t write_pos =
      atomic_load_explicit(&ring->write_pos, memory_order_acquire);
  if (read_pos == write_pos) {
    return NULL;
  }

  // Load device pointer (acquire ensures it's visible)
  void *device = ring->items[read_pos % KIRQ_RING_SIZE];

  // Advance read position
  atomic_store_explicit(&ring->read_pos, read_pos + 1, memory_order_release);
  return device;
}

uint32_t kirq_ring_overflow_count(const kirq_ring_t *ring) {
  return atomic_load_explicit(&ring->overflow_count, memory_order_relaxed);
}

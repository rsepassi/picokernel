#include "irq_ring.h"
#include <stddef.h>

void kirq_ring_init(kirq_ring_t *ring) {
  ring->write_pos = 0;
  ring->read_pos = 0;
  ring->overflow_count = 0;
}

bool kirq_ring_enqueue(kirq_ring_t *ring, void *device) {
#if KIRQ_RING_USE_ATOMICS
  // C11 atomics implementation (acquire/release ordering)
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

#else
  // Volatile + barriers implementation
  uint32_t write_pos = ring->write_pos;
  uint32_t read_pos = ring->read_pos;
  uint32_t next_write = write_pos + 1;

  // Check for overflow
  if (next_write - read_pos >= KIRQ_RING_SIZE) {
    ring->overflow_count++;
    return false;
  }

  // Store device pointer
  ring->items[write_pos % KIRQ_RING_SIZE] = device;

  // Full memory barrier (ensures item is visible before updating write_pos)
  __sync_synchronize();

  // Update write position
  ring->write_pos = next_write;
  return true;
#endif
}

void *kirq_ring_dequeue(kirq_ring_t *ring) {
#if KIRQ_RING_USE_ATOMICS
  // C11 atomics implementation
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

#else
  // Volatile + barriers implementation
  uint32_t read_pos = ring->read_pos;
  uint32_t write_pos = ring->write_pos;

  // Check if empty
  if (read_pos == write_pos) {
    return NULL;
  }

  // Load device pointer (dependency ordering via read_pos != write_pos)
  void *device = ring->items[read_pos % KIRQ_RING_SIZE];

  // Advance read position (no barrier needed here - consumer is
  // single-threaded)
  ring->read_pos = read_pos + 1;
  return device;
#endif
}

bool kirq_ring_is_empty(const kirq_ring_t *ring) {
#if KIRQ_RING_USE_ATOMICS
  uint32_t read_pos =
      atomic_load_explicit(&ring->read_pos, memory_order_relaxed);
  uint32_t write_pos =
      atomic_load_explicit(&ring->write_pos, memory_order_acquire);
  return read_pos == write_pos;
#else
  return ring->read_pos == ring->write_pos;
#endif
}

uint32_t kirq_ring_snapshot(const kirq_ring_t *ring) {
#if KIRQ_RING_USE_ATOMICS
  // Acquire ordering ensures we see all enqueued items up to this point
  return atomic_load_explicit(&ring->write_pos, memory_order_acquire);
#else
  return ring->write_pos;
#endif
}

void *kirq_ring_dequeue_bounded(kirq_ring_t *ring, uint32_t end_pos) {
#if KIRQ_RING_USE_ATOMICS
  // C11 atomics implementation
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

#else
  // Volatile + barriers implementation
  uint32_t read_pos = ring->read_pos;

  // Check if we've reached the end position
  if (read_pos == end_pos) {
    return NULL;
  }

  // Also check if ring is empty
  uint32_t write_pos = ring->write_pos;
  if (read_pos == write_pos) {
    return NULL;
  }

  // Load device pointer (dependency ordering via read_pos != write_pos)
  void *device = ring->items[read_pos % KIRQ_RING_SIZE];

  // Advance read position (no barrier needed here - consumer is
  // single-threaded)
  ring->read_pos = read_pos + 1;
  return device;
#endif
}

uint32_t kirq_ring_overflow_count(const kirq_ring_t *ring) {
#if KIRQ_RING_USE_ATOMICS
  return atomic_load_explicit(&ring->overflow_count, memory_order_relaxed);
#else
  return ring->overflow_count;
#endif
}

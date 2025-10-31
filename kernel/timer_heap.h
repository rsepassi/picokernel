// vmos Timer Heap
// Intrusive pointer-based min-heap for O(log n) timer operations

#pragma once

#include "kernel.h"

// Insert a timer into the heap (O(log n))
void timer_heap_insert(kernel_t *k, ktimer_req_t *timer);

// Extract the minimum (earliest deadline) timer from the heap (O(log n))
ktimer_req_t *timer_heap_extract_min(kernel_t *k);

// Delete an arbitrary timer from the heap (O(log n)) - used for cancellation
void timer_heap_delete(kernel_t *k, ktimer_req_t *timer);

// Peek at the minimum timer without removing it (O(1))
static inline ktimer_req_t *timer_heap_peek_min(kernel_t *k) {
  return k->timer_heap_root;
}

// vmos Timer Heap Implementation
// Intrusive pointer-based min-heap for O(log n) timer operations

#include "timer_heap.h"

// Navigate to a specific position in the heap tree using binary path
// Position is 1-indexed (1 = root, 2 = left child of root, 3 = right child,
// etc.)
static ktimer_req_t *timer_heap_navigate(ktimer_req_t *root, size_t position) {
  if (position == 0 || root == NULL) {
    return NULL;
  }

  if (position == 1) {
    return root;
  }

  // Find the most significant bit position (tree depth)
  size_t msb = 0;
  size_t temp = position;
  while (temp > 1) {
    temp >>= 1;
    msb++;
  }

  // Navigate using bits from msb-1 down to 0
  ktimer_req_t *current = root;
  for (int i = msb - 1; i >= 0 && current != NULL; i--) {
    if (position & (1UL << i)) {
      current = current->right;
    } else {
      current = current->left;
    }
  }

  return current;
}

// Find parent of a position (NULL for root)
static ktimer_req_t *timer_heap_find_parent(ktimer_req_t *root,
                                            size_t position) {
  if (position <= 1) {
    return NULL;
  }
  return timer_heap_navigate(root, position / 2);
}

// Find the insertion point for a new node (parent of position size+1)
static ktimer_req_t **timer_heap_find_insertion_slot(kernel_t *k) {
  size_t new_position = k->timer_heap_size + 1;

  if (new_position == 1) {
    return &k->timer_heap_root;
  }

  ktimer_req_t *parent =
      timer_heap_find_parent(k->timer_heap_root, new_position);
  if (parent == NULL) {
    return NULL;
  }

  // Determine if new node should be left or right child
  if (new_position % 2 == 0) {
    return &parent->left;
  } else {
    return &parent->right;
  }
}

// Find the last node in the heap (at position size)
static ktimer_req_t *timer_heap_find_last(kernel_t *k) {
  if (k->timer_heap_size == 0) {
    return NULL;
  }
  return timer_heap_navigate(k->timer_heap_root, k->timer_heap_size);
}

// Swap the data (deadline and work) between two timer nodes
static void timer_heap_swap(ktimer_req_t *a, ktimer_req_t *b) {
  // Swap deadline
  uint64_t temp_deadline = a->deadline_ns;
  a->deadline_ns = b->deadline_ns;
  b->deadline_ns = temp_deadline;

  // Swap work item data (but not the next/prev list pointers)
  kwork_t temp_work = a->work;
  a->work = b->work;
  b->work = temp_work;
}

// Bubble up to restore min-heap property
static void timer_heap_bubble_up(ktimer_req_t *node) {
  while (node->parent != NULL &&
         node->deadline_ns < node->parent->deadline_ns) {
    timer_heap_swap(node, node->parent);
    node = node->parent;
  }
}

// Bubble down to restore min-heap property
static void timer_heap_bubble_down(ktimer_req_t *node) {
  while (1) {
    ktimer_req_t *smallest = node;

    if (node->left != NULL && node->left->deadline_ns < smallest->deadline_ns) {
      smallest = node->left;
    }

    if (node->right != NULL &&
        node->right->deadline_ns < smallest->deadline_ns) {
      smallest = node->right;
    }

    if (smallest == node) {
      break; // Heap property satisfied
    }

    timer_heap_swap(node, smallest);
    node = smallest;
  }
}

// Insert a timer into the heap
void timer_heap_insert(kernel_t *k, ktimer_req_t *timer) {
  // Initialize tree pointers
  timer->parent = NULL;
  timer->left = NULL;
  timer->right = NULL;

  // Find insertion slot
  ktimer_req_t **slot = timer_heap_find_insertion_slot(k);
  if (slot == NULL) {
    return; // Should never happen
  }

  // Insert at the slot
  *slot = timer;

  // Set parent pointer
  if (k->timer_heap_size > 0) {
    timer->parent =
        timer_heap_find_parent(k->timer_heap_root, k->timer_heap_size + 1);
  }

  k->timer_heap_size++;

  // Restore heap property
  timer_heap_bubble_up(timer);
}

// Extract the minimum (root) timer from the heap
ktimer_req_t *timer_heap_extract_min(kernel_t *k) {
  if (k->timer_heap_root == NULL) {
    return NULL;
  }

  ktimer_req_t *min_timer = k->timer_heap_root;

  if (k->timer_heap_size == 1) {
    // Only one element
    k->timer_heap_root = NULL;
    k->timer_heap_size = 0;
    return min_timer;
  }

  // Find last node
  ktimer_req_t *last = timer_heap_find_last(k);
  if (last == NULL) {
    return NULL;
  }

  // Swap root with last node
  timer_heap_swap(min_timer, last);

  // Remove last node from tree
  if (last->parent != NULL) {
    if (last->parent->left == last) {
      last->parent->left = NULL;
    } else {
      last->parent->right = NULL;
    }
  }

  k->timer_heap_size--;

  // Restore heap property from root
  if (k->timer_heap_root != NULL) {
    timer_heap_bubble_down(k->timer_heap_root);
  }

  return min_timer;
}

// Delete an arbitrary timer from the heap (for cancellation)
void timer_heap_delete(kernel_t *k, ktimer_req_t *timer) {
  if (timer == NULL || k->timer_heap_size == 0) {
    return;
  }

  // If it's the only node
  if (k->timer_heap_size == 1) {
    k->timer_heap_root = NULL;
    k->timer_heap_size = 0;
    return;
  }

  // Find last node
  ktimer_req_t *last = timer_heap_find_last(k);
  if (last == NULL) {
    return;
  }

  // If deleting the last node, just remove it
  if (timer == last) {
    if (last->parent != NULL) {
      if (last->parent->left == last) {
        last->parent->left = NULL;
      } else {
        last->parent->right = NULL;
      }
    }
    k->timer_heap_size--;
    return;
  }

  // Swap timer with last node
  timer_heap_swap(timer, last);

  // Remove last node
  if (last->parent != NULL) {
    if (last->parent->left == last) {
      last->parent->left = NULL;
    } else {
      last->parent->right = NULL;
    }
  }

  k->timer_heap_size--;

  // Restore heap property - try both up and down
  if (timer->parent != NULL &&
      timer->deadline_ns < timer->parent->deadline_ns) {
    timer_heap_bubble_up(timer);
  } else {
    timer_heap_bubble_down(timer);
  }
}

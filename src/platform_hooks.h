// Platform Hooks Interface
// Architecture-specific operations abstraction for VirtIO and other generic code

#pragma once

#include <stddef.h>
#include <stdint.h>

// Platform hooks for architecture-specific operations
// Each platform (x64, arm64, rv64) implements these functions

// Memory synchronization (cache coherency)
void platform_cache_clean(void *addr, size_t size);      // Flush to RAM (before device reads)
void platform_cache_invalidate(void *addr, size_t size); // Discard cache (before CPU reads)

// Memory barriers
void platform_memory_barrier(void); // Full memory barrier

// Interrupt registration
int platform_irq_register(uint32_t irq_num, void (*handler)(void *), void *context);
void platform_irq_enable(uint32_t irq_num);

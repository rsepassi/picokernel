// rv32 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include <stdint.h>

// rv32 platform-specific state
typedef struct {
  uint32_t last_interrupt; // Last interrupt reason code
  uint64_t timer_freq;     // Timer frequency in Hz (from devicetree)
  uint64_t ticks_per_ms;   // Precomputed ticks per millisecond
} platform_t;

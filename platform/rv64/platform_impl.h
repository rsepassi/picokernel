// RISC-V 64-bit Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include <stdint.h>

// rv64 platform-specific state
typedef struct {
  uint32_t last_interrupt; // Last interrupt reason code
  uint64_t timebase_freq;  // Timebase frequency in Hz (from device tree)
} platform_t;

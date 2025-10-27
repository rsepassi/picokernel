// ARM64 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include <stdint.h>

// ARM64 platform-specific state
// This structure can hold platform-specific data (for future use)
typedef struct {
    uint32_t last_interrupt;  // Last interrupt reason code
    uint64_t timer_freq_hz;   // Timer frequency from CNTFRQ_EL0
    // Future: could add GIC base addresses, device state, etc.
} platform_t;

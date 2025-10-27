// ARM32 Platform Implementation
// Platform-specific inline functions and definitions

#pragma once

#include <stdint.h>

// ARM32 platform-specific state
// This structure can hold platform-specific data (for future use)
typedef struct {
    uint32_t last_interrupt;  // Last interrupt reason code
    // Future: could add GIC state, device state, etc.
} platform_t;

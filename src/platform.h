// Platform abstraction layer
#pragma once

#include <stdint.h>
#include <stddef.h>

// Each platform implements platform_impl.h with platform-specific features
#include "platform_impl.h"

// Interrupt reason codes returned by platform_wfi
typedef enum {
    PLATFORM_INT_TIMEOUT = 0,    // Timeout expired
    PLATFORM_INT_TIMER = 1,      // Timer interrupt
    PLATFORM_INT_DEVICE = 2,     // Device interrupt
    PLATFORM_INT_IPI = 3,        // Inter-processor interrupt
    PLATFORM_INT_EXCEPTION = 4,  // Exception occurred
    PLATFORM_INT_UNKNOWN = 0xFF  // Unknown/unhandled interrupt
} platform_int_reason_t;

// Initialize platform-specific features (interrupts, timers, device enumeration)
void platform_init(platform_t* platform, void* fdt);

// Wait for interrupt with timeout
// timeout_ms: timeout in milliseconds (UINT64_MAX = wait forever)
// Returns: reason code indicating what interrupt fired
uint32_t platform_wfi(platform_t* platform, uint64_t timeout_ms);

// Convert interrupt reason code to human-readable string
const char* platform_int_reason_str(uint32_t reason);


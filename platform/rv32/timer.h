// RISC-V Timer Functions
// SBI-based timer interface

#pragma once

#include <stdint.h>

// Forward declaration (must match platform_impl.h exactly)
typedef struct platform_t platform_t;

// Initialize timer subsystem
// Reads timebase-frequency from devicetree and calibrates
void timer_init(platform_t *platform, void *fdt);

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(platform_t *platform, uint32_t milliseconds);

// Timer interrupt handler (called from trap handler)
void timer_interrupt_handler(platform_t *platform);

// Get current time in milliseconds
uint64_t timer_get_current_time_ms(platform_t *platform);

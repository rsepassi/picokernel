// x32 Local APIC Timer
// Timer driver using the CPU's Local APIC timer

#pragma once

#include <stdint.h>

// Forward declaration
typedef struct platform_t platform_t;

// Initialize the Local APIC timer
void timer_init(platform_t *platform);

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(platform_t *platform, uint32_t milliseconds,
                          void (*callback)(void));

// Get current time in milliseconds
uint64_t timer_get_current_time_ms(platform_t *platform);

// Cancel any pending timer
void timer_cancel(platform_t *platform);

// LAPIC timer interrupt handler
void lapic_timer_handler(platform_t *platform);

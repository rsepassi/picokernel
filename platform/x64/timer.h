// x64 Local APIC Timer
// Timer driver using the CPU's Local APIC timer

#pragma once

#include "platform.h"
#include "platform_impl.h"

// Initialize the Local APIC timer
void timer_init(platform_t *platform);

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(platform_t *platform, uint32_t milliseconds,
                          timer_callback_t callback);

// Get current time in nanoseconds
ktime_t timer_get_current_time_ns(platform_t *platform);

// Cancel any pending timer
void timer_cancel(platform_t *platform);

// LAPIC timer interrupt handler
void lapic_timer_handler(platform_t *platform);

// ARM64 Generic Timer
// Timer driver using the ARM architected Generic Timer

#pragma once

#include "platform.h"
#include "platform_impl.h"

// Initialize the Generic Timer
void timer_init(platform_t *platform);

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(platform_t *platform, uint32_t milliseconds,
                          timer_callback_t callback);

// Get timer frequency in Hz
uint64_t timer_get_frequency(platform_t *platform);

// Get current time in nanoseconds
ktime_t timer_get_current_time_ns(platform_t *platform);

// Cancel any pending timer
void timer_cancel(platform_t *platform);

// Timer interrupt handler (called from interrupt.c via IRQ dispatch)
void generic_timer_handler(void *context);

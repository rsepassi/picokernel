// RISC-V SBI Timer Driver
// Uses SBI timer interface for interrupts

#pragma once

#include "platform.h"
#include <stdint.h>

// timer_callback_t is defined in platform_impl.h

// Initialize the timer (calibrate frequency from device tree)
void timer_init(platform_t *platform, void *fdt);

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(platform_t *platform, uint32_t milliseconds,
                          timer_callback_t callback);

// Get current timer frequency in Hz
uint64_t timer_get_frequency(platform_t *platform);

// Get current time in milliseconds
uint64_t timer_get_current_time_ms(platform_t *platform);

// Get current time in nanoseconds
ktime_t timer_get_current_time_ns(platform_t *platform);

// Cancel any pending timer
void timer_cancel(platform_t *platform);

// Timer interrupt handler (called from interrupt.c's trap_handler)
void timer_handler(platform_t *platform);

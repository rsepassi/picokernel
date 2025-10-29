// ARM32 Generic Timer
// Timer driver using ARM Generic Timer (ARMv7-A)

#pragma once

#include <stdint.h>
#include "platform_impl.h"

// timer_callback_t is defined in platform_impl.h

// Initialize the ARM Generic Timer
void timer_init(platform_t *platform);

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(platform_t *platform, uint32_t milliseconds,
                          timer_callback_t callback);

// Get the timer frequency in Hz
uint32_t timer_get_frequency(platform_t *platform);

// Get current time in milliseconds
uint64_t timer_get_current_time_ms(platform_t *platform);

// Timer interrupt handler (called from interrupt controller)
void timer_handler(platform_t *platform);

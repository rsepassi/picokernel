// ARM64 Generic Timer
// Timer driver using the ARM architected Generic Timer

#pragma once

#include <stdint.h>

// Callback function type for timer events
typedef void (*timer_callback_t)(void);

// Initialize the Generic Timer
void timer_init(void);

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(uint32_t milliseconds, timer_callback_t callback);

// Get timer frequency in Hz
uint64_t timer_get_frequency(void);

// Get current time in milliseconds
uint64_t timer_get_current_time_ms(void);

// Timer interrupt handler (called from interrupt.c)
void generic_timer_handler(void);

// RISC-V SBI Timer Driver
// Uses SBI timer interface for interrupts

#pragma once

#include <stdint.h>

// Callback function type for timer events
typedef void (*timer_callback_t)(void);

// Initialize the timer (calibrate frequency from device tree)
void timer_init(void* fdt, uint64_t* out_freq);

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(uint32_t milliseconds, timer_callback_t callback);

// Get current timer frequency in Hz
uint64_t timer_get_frequency(void);

// RISC-V Timer Functions
// SBI-based timer interface

#pragma once

#include <stdint.h>

// Timer callback function type
typedef void (*timer_callback_t)(void);

// Initialize timer subsystem
// Reads timebase-frequency from devicetree and calibrates
void timer_init(void *fdt);

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(uint32_t milliseconds, timer_callback_t callback);

// Timer interrupt handler (called from trap handler)
void timer_interrupt_handler(void);

// Get timer frequency in Hz
uint64_t timer_get_frequency(void);

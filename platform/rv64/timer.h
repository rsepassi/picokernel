// RISC-V SBI Timer Driver
// Uses SBI timer interface for interrupts

#pragma once

#include <stdint.h>

// Forward declaration (complete type defined in platform_impl.h)
struct platform_t;

// Initialize the timer (calibrate frequency from device tree)
void timer_init(struct platform_t *platform, void *fdt);

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(struct platform_t *platform, uint32_t milliseconds);

// Get current timer frequency in Hz
uint64_t timer_get_frequency(struct platform_t *platform);

// Get current time in milliseconds
uint64_t timer_get_current_time_ms(struct platform_t *platform);

// Timer interrupt handler (called from interrupt.c's trap_handler)
void timer_handler(struct platform_t *platform);

// x64 Local APIC Timer
// Timer driver using the CPU's Local APIC timer

#pragma once

#include <stdint.h>

// Callback function type for timer events
typedef void (*timer_callback_t)(void);

// Initialize the Local APIC timer
void timer_init(void);

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(uint32_t milliseconds, timer_callback_t callback);

// Get current time in milliseconds (uptime since timer_init)
uint64_t timer_get_current_time_ms(void);

// Send EOI (End Of Interrupt) to Local APIC
void lapic_send_eoi(void);

void lapic_timer_handler(void);

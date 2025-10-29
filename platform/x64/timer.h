// x64 Local APIC Timer
// Timer driver using the CPU's Local APIC timer

#pragma once

#include <stdint.h>

// Forward declaration and typedef for platform
struct platform_t;
typedef struct platform_t platform_t;

// Callback function type for timer events
typedef void (*timer_callback_t)(void);

// Initialize the Local APIC timer
void timer_init(platform_t *platform);

// Set a one-shot timer to fire after specified milliseconds
void timer_set_oneshot_ms(platform_t *platform, uint32_t milliseconds, timer_callback_t callback);

// Get current time in milliseconds (uptime since timer_init)
uint64_t timer_get_current_time_ms(platform_t *platform);

// Send EOI (End Of Interrupt) to Local APIC
void lapic_send_eoi(platform_t *platform);

void lapic_timer_handler(platform_t *platform);

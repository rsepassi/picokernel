// Platform abstraction layer
#pragma once

#include "kbase.h"

// Each platform implements platform_impl.h with platform-specific features
#include "platform_impl.h"

// Initialize platform-specific features (interrupts, timers, device enumeration)
void platform_init(platform_t* platform, void* fdt);

// Wait for interrupt with timeout
// timeout_ms: timeout in milliseconds (UINT64_MAX = wait forever)
// Returns: current time in milliseconds
uint64_t platform_wfi(platform_t* platform, uint64_t timeout_ms);

// Interrupt control
void interrupt_enable(void);
void interrupt_disable(void);

void uart_puts(const char *str);
void uart_putc(char c);
void fdt_dump(void* fdt);

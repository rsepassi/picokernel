// Platform abstraction layer
#pragma once

#include "kbase.h"

// Each platform implements platform_impl.h with platform-specific features
#include "platform_impl.h"

// Platform Contract Documentation: See src/platform_contract.h
// That file documents all functions that platforms must implement.

// Initialize platform-specific features (interrupts, timers, device
// enumeration)
void platform_init(platform_t *platform, void *fdt);

// Wait for interrupt with timeout
// timeout_ms: timeout in milliseconds (UINT64_MAX = wait forever)
// Returns: current time in milliseconds
uint64_t platform_wfi(platform_t *platform, uint64_t timeout_ms);

// Interrupt control
void platform_interrupt_enable(void);
void platform_interrupt_disable(void);

void platform_uart_puts(const char *str);
void platform_uart_putc(char c);
void platform_fdt_dump(void *fdt);

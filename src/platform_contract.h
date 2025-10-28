// Platform Contract
//
// This header documents all functions that platform implementations
// MUST provide. Each platform (x64, arm64, etc.) must implement
// these functions.
//
// Note: This is documentation only. Actual type definitions come from
// kapi.h (platform_t, kernel_t, kwork_t).

#pragma once

#include <stddef.h>
#include <stdint.h>

// Forward declarations for documentation purposes
// (Actual types are defined in kapi.h)
typedef struct platform platform_t;
typedef struct kernel kernel_t;
typedef struct kwork kwork_t;

/* Platform lifecycle */
void platform_init(platform_t *platform, void *fdt);

/* Interrupt control */
void platform_interrupt_enable(void);
void platform_interrupt_disable(void);

/* Wait for interrupt with optional timeout */
uint64_t platform_wfi(platform_t *platform, uint64_t timeout_ms);

/* Work submission - process work queue changes */
void platform_submit(platform_t *platform,
                     kwork_t *submissions,
                     kwork_t *cancellations);

/* UART output */
void platform_uart_puts(const char *str);
void platform_uart_putc(char c);

/* Device tree */
void platform_fdt_dump(void *fdt);

/* Cache operations (platform_hooks.h) */
void platform_cache_clean(void *addr, size_t size);
void platform_cache_invalidate(void *addr, size_t size);
void platform_memory_barrier(void);

/* IRQ management (platform_hooks.h) */
int platform_irq_register(uint32_t irq_num, void (*handler)(void *), void *context);
void platform_irq_enable(uint32_t irq_num);

/* Platform tick - called by kernel during tick processing */
void kplatform_tick(platform_t *platform, kernel_t *k);

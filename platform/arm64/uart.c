// PL011 UART driver for debugging
// The ARM virt machine has a PL011 UART at 0x09000000

#include "kernel.h"
#include <stdint.h>

// Default UART address for QEMU virt machine
// This is used until FDT scan discovers the actual address
#define UART_DEFAULT_BASE 0x09000000UL

// UART registers
#define UART_DR 0x00 // Data register
#define UART_FR 0x18 // Flag register

#define UART_FR_TXFF (1 << 5) // Transmit FIFO full

// Global UART base address (initialized to default, updated from FDT)
static uintptr_t g_uart_base = UART_DEFAULT_BASE;

void platform_uart_putc(char c) {
  volatile uint32_t *uart = (volatile uint32_t *)g_uart_base;

  // Wait for TX FIFO to have space
  while (uart[UART_FR / 4] & UART_FR_TXFF) {
    // Wait
  }

  // Write character
  uart[UART_DR / 4] = c;
}

void platform_uart_puts(const char *str) {
  while (*str) {
    if (*str == '\n') {
      platform_uart_putc('\r');
    }
    platform_uart_putc(*str++);
  }
}

// Initialize/update UART base address from platform context
// This should be called after FDT parsing completes
void platform_uart_init(platform_t *platform) {
  if (platform && platform->uart_base != 0) {
    g_uart_base = platform->uart_base;
  }
}

// PL011 UART driver for debugging
// The ARM virt machine has a PL011 UART at 0x09000000

#include <stdint.h>

#define UART_BASE 0x09000000UL

// UART registers
#define UART_DR 0x00 // Data register
#define UART_FR 0x18 // Flag register

#define UART_FR_TXFF (1 << 5) // Transmit FIFO full

void platform_uart_putc(char c) {
  volatile uint32_t *uart = (volatile uint32_t *)UART_BASE;

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

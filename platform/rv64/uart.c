// RISC-V 64-bit UART driver
// NS16550A UART for QEMU virt machine

#include "platform.h"
#include <stdint.h>

// UART base address (initialized from platform_t during boot)
// Default is QEMU virt address for early boot before FDT parsing
static volatile uintptr_t g_uart_base = 0x10000000UL;

// UART register offsets
#define UART_RBR 0x00 // Receive Buffer Register (read)
#define UART_THR 0x00 // Transmit Holding Register (write)
#define UART_IER 0x01 // Interrupt Enable Register
#define UART_FCR 0x02 // FIFO Control Register
#define UART_LCR 0x03 // Line Control Register
#define UART_MCR 0x04 // Modem Control Register
#define UART_LSR 0x05 // Line Status Register
#define UART_MSR 0x06 // Modem Status Register
#define UART_SCR 0x07 // Scratch Register

// Line Status Register bits
#define UART_LSR_THRE (1 << 5) // Transmit Holding Register Empty
#define UART_LSR_DR (1 << 0)   // Data Ready

static inline void uart_write_reg(unsigned int offset, unsigned char value) {
  volatile unsigned char *reg =
      (volatile unsigned char *)(g_uart_base + offset);
  *reg = value;
}

static inline unsigned char uart_read_reg(unsigned int offset) {
  volatile unsigned char *reg =
      (volatile unsigned char *)(g_uart_base + offset);
  return *reg;
}

// Initialize UART with discovered base address from platform_t
void platform_uart_init(platform_t *platform) {
  if (platform && platform->uart_base != 0) {
    g_uart_base = platform->uart_base;
  }
}

void platform_uart_putc(char c) {
  // Wait until the transmit holding register is empty
  while ((uart_read_reg(UART_LSR) & UART_LSR_THRE) == 0) {
    // Busy wait
  }

  // Write the character
  uart_write_reg(UART_THR, (unsigned char)c);
}

void platform_uart_puts(const char *str) {
  while (*str) {
    if (*str == '\n') {
      platform_uart_putc('\r'); // Add carriage return before newline
    }
    platform_uart_putc(*str++);
  }
}

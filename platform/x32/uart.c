// x32 UART driver
// 8250/16550 serial port (COM1) for QEMU microvm/q35
// (Identical to x64 version - no pointer-dependent code)

#include "io.h"

// COM1 serial port I/O port base address
#define COM1_PORT 0x3F8

// Serial port register offsets
#define UART_DATA 0    // Data register (read/write)
#define UART_IER 1     // Interrupt Enable Register
#define UART_FCR 2     // FIFO Control Register
#define UART_LCR 3     // Line Control Register
#define UART_MCR 4     // Modem Control Register
#define UART_LSR 5     // Line Status Register
#define UART_MSR 6     // Modem Status Register
#define UART_SCRATCH 7 // Scratch Register

// Line Status Register bits
#define UART_LSR_THRE (1 << 5) // Transmit Holding Register Empty
#define UART_LSR_DR (1 << 0)   // Data Ready

void platform_uart_putc(char c) {
  // Wait until the transmit holding register is empty
  while ((inb(COM1_PORT + UART_LSR) & UART_LSR_THRE) == 0) {
    // Busy wait
  }

  // Write the character
  outb(COM1_PORT + UART_DATA, (unsigned char)c);
}

void platform_uart_puts(const char *str) {
  while (*str) {
    if (*str == '\n') {
      platform_uart_putc('\r'); // Add carriage return before newline
    }
    platform_uart_putc(*str++);
  }
}

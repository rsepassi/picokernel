// RISC-V 32-bit UART driver
// NS16550A UART for QEMU virt machine

// NS16550A UART base address for RISC-V virt machine
#define UART_BASE 0x10000000UL

// UART register offsets
#define UART_RBR 0x00  // Receive Buffer Register (read)
#define UART_THR 0x00  // Transmit Holding Register (write)
#define UART_IER 0x01  // Interrupt Enable Register
#define UART_FCR 0x02  // FIFO Control Register
#define UART_LCR 0x03  // Line Control Register
#define UART_MCR 0x04  // Modem Control Register
#define UART_LSR 0x05  // Line Status Register
#define UART_MSR 0x06  // Modem Status Register
#define UART_SCR 0x07  // Scratch Register

// Line Status Register bits
#define UART_LSR_THRE (1 << 5)  // Transmit Holding Register Empty
#define UART_LSR_DR   (1 << 0)  // Data Ready

static inline void uart_write_reg(unsigned int offset, unsigned char value)
{
    volatile unsigned char *reg = (volatile unsigned char *)(UART_BASE + offset);
    *reg = value;
}

static inline unsigned char uart_read_reg(unsigned int offset)
{
    volatile unsigned char *reg = (volatile unsigned char *)(UART_BASE + offset);
    return *reg;
}

void uart_putc(char c)
{
    // Wait until the transmit holding register is empty
    while ((uart_read_reg(UART_LSR) & UART_LSR_THRE) == 0) {
        // Busy wait
    }

    // Write the character
    uart_write_reg(UART_THR, (unsigned char)c);
}

void uart_puts(const char *str)
{
    while (*str) {
        if (*str == '\n') {
            uart_putc('\r');  // Add carriage return before newline
        }
        uart_putc(*str++);
    }
}

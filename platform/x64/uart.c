// x86-64 UART driver
// 8250/16550 serial port (COM1) for QEMU microvm/q35

// COM1 serial port I/O port base address
#define COM1_PORT 0x3F8

// Serial port register offsets
#define UART_DATA    0  // Data register (read/write)
#define UART_IER     1  // Interrupt Enable Register
#define UART_FCR     2  // FIFO Control Register
#define UART_LCR     3  // Line Control Register
#define UART_MCR     4  // Modem Control Register
#define UART_LSR     5  // Line Status Register
#define UART_MSR     6  // Modem Status Register
#define UART_SCRATCH 7  // Scratch Register

// Line Status Register bits
#define UART_LSR_THRE (1 << 5)  // Transmit Holding Register Empty
#define UART_LSR_DR   (1 << 0)  // Data Ready

// I/O port access functions
static inline void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void uart_putc(char c)
{
    // Wait until the transmit holding register is empty
    while ((inb(COM1_PORT + UART_LSR) & UART_LSR_THRE) == 0) {
        // Busy wait
    }

    // Write the character
    outb(COM1_PORT + UART_DATA, (unsigned char)c);
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

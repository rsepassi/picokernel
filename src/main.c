// vmos kernel entry point

void uart_puts(const char*);

void main(void)
{
    uart_puts("Hello World from vmos!\n");

    // Infinite loop
    while (1) {
        // Empty loop
    }
}

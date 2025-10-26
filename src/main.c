// vmos kernel entry point

void uart_puts(const char*);
void fdt_dump(void* fdt);

void main(void* fdt)
{
    uart_puts("vmos kernel starting...\n\n");

    // Parse and display device tree
    fdt_dump(fdt);

    uart_puts("Kernel initialization complete.\n");

    // Infinite loop
    while (1) {
        // Empty loop
    }
}

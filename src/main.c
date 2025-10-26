// vmos kernel entry point

#include "uart.h"

void main(void)
{
    uart_puts("Hello World from vmos!\n");

    // Infinite loop
    while (1) {
        // Empty loop
    }
}

// vmos kernel entry point

#include "printk.h"

void fdt_dump(void* fdt);

void main(void* fdt)
{
    printk("vmos kernel starting...\n\n");

    // Parse and display device tree
    fdt_dump(fdt);

    printk("Kernel initialization complete.\n");

    // Infinite loop
    while (1) {
        // Empty loop
    }
}

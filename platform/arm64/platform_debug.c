// ARM64 Platform Debug Support
// Register and stack dumps for panic handler

#include "platform.h"
#include "printk.h"
#include <stdint.h>

// Dump ARM64 registers to debug console
void platform_dump_registers(void) {
  uint64_t x0, x1, x2, x3, x4, x5, x6, x7;
  uint64_t x8, x9, x10, x11, x12, x13, x14, x15;
  uint64_t x16, x17, x18, x19, x20, x21, x22, x23;
  uint64_t x24, x25, x26, x27, x28, x29, x30;
  uint64_t sp, pc;

  // Read registers using inline assembly
  __asm__ volatile("mov %0, x0" : "=r"(x0));
  __asm__ volatile("mov %0, x1" : "=r"(x1));
  __asm__ volatile("mov %0, x2" : "=r"(x2));
  __asm__ volatile("mov %0, x3" : "=r"(x3));
  __asm__ volatile("mov %0, x4" : "=r"(x4));
  __asm__ volatile("mov %0, x5" : "=r"(x5));
  __asm__ volatile("mov %0, x6" : "=r"(x6));
  __asm__ volatile("mov %0, x7" : "=r"(x7));
  __asm__ volatile("mov %0, x8" : "=r"(x8));
  __asm__ volatile("mov %0, x9" : "=r"(x9));
  __asm__ volatile("mov %0, x10" : "=r"(x10));
  __asm__ volatile("mov %0, x11" : "=r"(x11));
  __asm__ volatile("mov %0, x12" : "=r"(x12));
  __asm__ volatile("mov %0, x13" : "=r"(x13));
  __asm__ volatile("mov %0, x14" : "=r"(x14));
  __asm__ volatile("mov %0, x15" : "=r"(x15));
  __asm__ volatile("mov %0, x16" : "=r"(x16));
  __asm__ volatile("mov %0, x17" : "=r"(x17));
  __asm__ volatile("mov %0, x18" : "=r"(x18));
  __asm__ volatile("mov %0, x19" : "=r"(x19));
  __asm__ volatile("mov %0, x20" : "=r"(x20));
  __asm__ volatile("mov %0, x21" : "=r"(x21));
  __asm__ volatile("mov %0, x22" : "=r"(x22));
  __asm__ volatile("mov %0, x23" : "=r"(x23));
  __asm__ volatile("mov %0, x24" : "=r"(x24));
  __asm__ volatile("mov %0, x25" : "=r"(x25));
  __asm__ volatile("mov %0, x26" : "=r"(x26));
  __asm__ volatile("mov %0, x27" : "=r"(x27));
  __asm__ volatile("mov %0, x28" : "=r"(x28));
  __asm__ volatile("mov %0, x29" : "=r"(x29)); // FP
  __asm__ volatile("mov %0, x30" : "=r"(x30)); // LR
  __asm__ volatile("mov %0, sp" : "=r"(sp));

  // PC approximation (we can't read PC directly, use return address)
  pc = x30;

  printk("Registers:\n");
  printk("  PC:  0x");
  printk_hex64(pc);
  printk("  SP:  0x");
  printk_hex64(sp);
  printk("  LR:  0x");
  printk_hex64(x30);
  printk("\n");

  printk("  FP:  0x");
  printk_hex64(x29);
  printk("\n");

  printk("  X0:  0x");
  printk_hex64(x0);
  printk("  X1:  0x");
  printk_hex64(x1);
  printk("  X2:  0x");
  printk_hex64(x2);
  printk("\n");

  printk("  X3:  0x");
  printk_hex64(x3);
  printk("  X4:  0x");
  printk_hex64(x4);
  printk("  X5:  0x");
  printk_hex64(x5);
  printk("\n");

  printk("  X6:  0x");
  printk_hex64(x6);
  printk("  X7:  0x");
  printk_hex64(x7);
  printk("  X8:  0x");
  printk_hex64(x8);
  printk("\n");

  printk("  X9:  0x");
  printk_hex64(x9);
  printk("  X10: 0x");
  printk_hex64(x10);
  printk("  X11: 0x");
  printk_hex64(x11);
  printk("\n");

  printk("  X12: 0x");
  printk_hex64(x12);
  printk("  X13: 0x");
  printk_hex64(x13);
  printk("  X14: 0x");
  printk_hex64(x14);
  printk("\n");

  printk("  X15: 0x");
  printk_hex64(x15);
  printk("  X16: 0x");
  printk_hex64(x16);
  printk("  X17: 0x");
  printk_hex64(x17);
  printk("\n");

  printk("  X18: 0x");
  printk_hex64(x18);
  printk("  X19: 0x");
  printk_hex64(x19);
  printk("  X20: 0x");
  printk_hex64(x20);
  printk("\n");

  printk("  X21: 0x");
  printk_hex64(x21);
  printk("  X22: 0x");
  printk_hex64(x22);
  printk("  X23: 0x");
  printk_hex64(x23);
  printk("\n");

  printk("  X24: 0x");
  printk_hex64(x24);
  printk("  X25: 0x");
  printk_hex64(x25);
  printk("  X26: 0x");
  printk_hex64(x26);
  printk("\n");

  printk("  X27: 0x");
  printk_hex64(x27);
  printk("  X28: 0x");
  printk_hex64(x28);
  printk("\n");
}

// Dump stack contents to debug console
void platform_dump_stack(uint32_t bytes) {
  uint64_t sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));

  printk("\nStack dump (");
  printk_dec(bytes);
  printk(" bytes from SP=0x");
  printk_hex64(sp);
  printk("):\n");

  uint8_t *stack = (uint8_t *)sp;
  for (uint32_t i = 0; i < bytes; i += 16) {
    printk("  0x");
    printk_hex64(sp + i);
    printk(": ");

    // Print 16 bytes in hex
    for (uint32_t j = 0; j < 16 && (i + j) < bytes; j++) {
      printk_hex8(stack[i + j]);
      printk(" ");
    }

    printk("\n");
  }
}

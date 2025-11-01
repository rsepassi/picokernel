// ARM32 Platform Debug Support
// Register and stack dumps for panic handler

#include "platform.h"
#include "printk.h"
#include <stdint.h>

// Dump ARM32 registers to debug console
void platform_dump_registers(void) {
  uint32_t r0, r1, r2, r3, r4, r5, r6, r7;
  uint32_t r8, r9, r10, r11, r12;
  uint32_t sp, lr, pc;

  // Read registers using inline assembly
  __asm__ volatile("mov %0, r0" : "=r"(r0));
  __asm__ volatile("mov %0, r1" : "=r"(r1));
  __asm__ volatile("mov %0, r2" : "=r"(r2));
  __asm__ volatile("mov %0, r3" : "=r"(r3));
  __asm__ volatile("mov %0, r4" : "=r"(r4));
  __asm__ volatile("mov %0, r5" : "=r"(r5));
  __asm__ volatile("mov %0, r6" : "=r"(r6));
  __asm__ volatile("mov %0, r7" : "=r"(r7));
  __asm__ volatile("mov %0, r8" : "=r"(r8));
  __asm__ volatile("mov %0, r9" : "=r"(r9));
  __asm__ volatile("mov %0, r10" : "=r"(r10));
  __asm__ volatile("mov %0, r11" : "=r"(r11)); // FP
  __asm__ volatile("mov %0, r12" : "=r"(r12));
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  __asm__ volatile("mov %0, lr" : "=r"(lr));

  // PC approximation (use LR as we can't read PC directly)
  pc = lr;

  printk("Registers:\n");
  printk("  PC:  0x");
  printk_hex32(pc);
  printk("  SP:  0x");
  printk_hex32(sp);
  printk("  LR:  0x");
  printk_hex32(lr);
  printk("\n");

  printk("  FP:  0x");
  printk_hex32(r11);
  printk("\n");

  printk("  R0:  0x");
  printk_hex32(r0);
  printk("  R1:  0x");
  printk_hex32(r1);
  printk("  R2:  0x");
  printk_hex32(r2);
  printk("\n");

  printk("  R3:  0x");
  printk_hex32(r3);
  printk("  R4:  0x");
  printk_hex32(r4);
  printk("  R5:  0x");
  printk_hex32(r5);
  printk("\n");

  printk("  R6:  0x");
  printk_hex32(r6);
  printk("  R7:  0x");
  printk_hex32(r7);
  printk("  R8:  0x");
  printk_hex32(r8);
  printk("\n");

  printk("  R9:  0x");
  printk_hex32(r9);
  printk("  R10: 0x");
  printk_hex32(r10);
  printk("  R12: 0x");
  printk_hex32(r12);
  printk("\n");
}

// Dump stack contents to debug console
void platform_dump_stack(uint32_t bytes) {
  uint32_t sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));

  printk("\nStack dump (");
  printk_dec(bytes);
  printk(" bytes from SP=0x");
  printk_hex32(sp);
  printk("):\n");

  uint8_t *stack = (uint8_t *)sp;
  for (uint32_t i = 0; i < bytes; i += 16) {
    printk("  0x");
    printk_hex32(sp + i);
    printk(": ");

    // Print 16 bytes in hex
    for (uint32_t j = 0; j < 16 && (i + j) < bytes; j++) {
      printk_hex8(stack[i + j]);
      printk(" ");
    }

    printk("\n");
  }
}

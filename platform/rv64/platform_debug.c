// RISC-V 64 Platform Debug Support
// Register and stack dumps for panic handler

#include "platform.h"
#include "printk.h"
#include <stdint.h>

// Dump RISC-V 64 registers to debug console
void platform_dump_registers(void) {
  uint64_t ra, sp, gp, tp;
  uint64_t t0, t1, t2, t3, t4, t5, t6;
  uint64_t s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
  uint64_t a0, a1, a2, a3, a4, a5, a6, a7;
  uint64_t pc;

  // Read registers using inline assembly
  __asm__ volatile("mv %0, ra" : "=r"(ra));
  __asm__ volatile("mv %0, sp" : "=r"(sp));
  __asm__ volatile("mv %0, gp" : "=r"(gp));
  __asm__ volatile("mv %0, tp" : "=r"(tp));
  __asm__ volatile("mv %0, t0" : "=r"(t0));
  __asm__ volatile("mv %0, t1" : "=r"(t1));
  __asm__ volatile("mv %0, t2" : "=r"(t2));
  __asm__ volatile("mv %0, t3" : "=r"(t3));
  __asm__ volatile("mv %0, t4" : "=r"(t4));
  __asm__ volatile("mv %0, t5" : "=r"(t5));
  __asm__ volatile("mv %0, t6" : "=r"(t6));
  __asm__ volatile("mv %0, s0" : "=r"(s0)); // FP
  __asm__ volatile("mv %0, s1" : "=r"(s1));
  __asm__ volatile("mv %0, s2" : "=r"(s2));
  __asm__ volatile("mv %0, s3" : "=r"(s3));
  __asm__ volatile("mv %0, s4" : "=r"(s4));
  __asm__ volatile("mv %0, s5" : "=r"(s5));
  __asm__ volatile("mv %0, s6" : "=r"(s6));
  __asm__ volatile("mv %0, s7" : "=r"(s7));
  __asm__ volatile("mv %0, s8" : "=r"(s8));
  __asm__ volatile("mv %0, s9" : "=r"(s9));
  __asm__ volatile("mv %0, s10" : "=r"(s10));
  __asm__ volatile("mv %0, s11" : "=r"(s11));
  __asm__ volatile("mv %0, a0" : "=r"(a0));
  __asm__ volatile("mv %0, a1" : "=r"(a1));
  __asm__ volatile("mv %0, a2" : "=r"(a2));
  __asm__ volatile("mv %0, a3" : "=r"(a3));
  __asm__ volatile("mv %0, a4" : "=r"(a4));
  __asm__ volatile("mv %0, a5" : "=r"(a5));
  __asm__ volatile("mv %0, a6" : "=r"(a6));
  __asm__ volatile("mv %0, a7" : "=r"(a7));

  // PC approximation (use RA as we can't read PC directly)
  pc = ra;

  printk("Registers:\n");
  printk("  PC:  0x");
  printk_hex64(pc);
  printk("  SP:  0x");
  printk_hex64(sp);
  printk("  RA:  0x");
  printk_hex64(ra);
  printk("\n");

  printk("  GP:  0x");
  printk_hex64(gp);
  printk("  TP:  0x");
  printk_hex64(tp);
  printk("  FP:  0x");
  printk_hex64(s0);
  printk("\n");

  printk("  A0:  0x");
  printk_hex64(a0);
  printk("  A1:  0x");
  printk_hex64(a1);
  printk("  A2:  0x");
  printk_hex64(a2);
  printk("\n");

  printk("  A3:  0x");
  printk_hex64(a3);
  printk("  A4:  0x");
  printk_hex64(a4);
  printk("  A5:  0x");
  printk_hex64(a5);
  printk("\n");

  printk("  A6:  0x");
  printk_hex64(a6);
  printk("  A7:  0x");
  printk_hex64(a7);
  printk("\n");

  printk("  T0:  0x");
  printk_hex64(t0);
  printk("  T1:  0x");
  printk_hex64(t1);
  printk("  T2:  0x");
  printk_hex64(t2);
  printk("\n");

  printk("  T3:  0x");
  printk_hex64(t3);
  printk("  T4:  0x");
  printk_hex64(t4);
  printk("  T5:  0x");
  printk_hex64(t5);
  printk("\n");

  printk("  T6:  0x");
  printk_hex64(t6);
  printk("\n");

  printk("  S1:  0x");
  printk_hex64(s1);
  printk("  S2:  0x");
  printk_hex64(s2);
  printk("  S3:  0x");
  printk_hex64(s3);
  printk("\n");

  printk("  S4:  0x");
  printk_hex64(s4);
  printk("  S5:  0x");
  printk_hex64(s5);
  printk("  S6:  0x");
  printk_hex64(s6);
  printk("\n");

  printk("  S7:  0x");
  printk_hex64(s7);
  printk("  S8:  0x");
  printk_hex64(s8);
  printk("  S9:  0x");
  printk_hex64(s9);
  printk("\n");

  printk("  S10: 0x");
  printk_hex64(s10);
  printk("  S11: 0x");
  printk_hex64(s11);
  printk("\n");
}

// Dump stack contents to debug console
void platform_dump_stack(uint32_t bytes) {
  uint64_t sp;
  __asm__ volatile("mv %0, sp" : "=r"(sp));

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

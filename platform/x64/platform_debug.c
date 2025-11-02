// x86/x64 Platform Debug Support
// Register and stack dumps for panic handler

#include "platform.h"
#include "printk.h"
#include <stdint.h>

// Dump x86/x64 registers to debug console
void platform_dump_registers(void) {
  uint64_t rip, rsp, rbp, rax, rbx, rcx, rdx;
  uint64_t rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15;
  uint64_t rflags;

  // Read registers using inline assembly
  __asm__ volatile("lea (%%rip), %0" : "=r"(rip));
  __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
  __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
  __asm__ volatile("mov %%rax, %0" : "=r"(rax));
  __asm__ volatile("mov %%rbx, %0" : "=r"(rbx));
  __asm__ volatile("mov %%rcx, %0" : "=r"(rcx));
  __asm__ volatile("mov %%rdx, %0" : "=r"(rdx));
  __asm__ volatile("mov %%rsi, %0" : "=r"(rsi));
  __asm__ volatile("mov %%rdi, %0" : "=r"(rdi));
  __asm__ volatile("mov %%r8, %0" : "=r"(r8));
  __asm__ volatile("mov %%r9, %0" : "=r"(r9));
  __asm__ volatile("mov %%r10, %0" : "=r"(r10));
  __asm__ volatile("mov %%r11, %0" : "=r"(r11));
  __asm__ volatile("mov %%r12, %0" : "=r"(r12));
  __asm__ volatile("mov %%r13, %0" : "=r"(r13));
  __asm__ volatile("mov %%r14, %0" : "=r"(r14));
  __asm__ volatile("mov %%r15, %0" : "=r"(r15));
  __asm__ volatile("pushfq; pop %0" : "=r"(rflags));

  printk("Registers:\n");
  printk("  RIP: 0x");
  printk_hex64(rip);
  printk("  RSP: 0x");
  printk_hex64(rsp);
  printk("  RBP: 0x");
  printk_hex64(rbp);
  printk("\n");

  printk("  RAX: 0x");
  printk_hex64(rax);
  printk("  RBX: 0x");
  printk_hex64(rbx);
  printk("  RCX: 0x");
  printk_hex64(rcx);
  printk("\n");

  printk("  RDX: 0x");
  printk_hex64(rdx);
  printk("  RSI: 0x");
  printk_hex64(rsi);
  printk("  RDI: 0x");
  printk_hex64(rdi);
  printk("\n");

  printk("  R8:  0x");
  printk_hex64(r8);
  printk("  R9:  0x");
  printk_hex64(r9);
  printk("  R10: 0x");
  printk_hex64(r10);
  printk("\n");

  printk("  R11: 0x");
  printk_hex64(r11);
  printk("  R12: 0x");
  printk_hex64(r12);
  printk("  R13: 0x");
  printk_hex64(r13);
  printk("\n");

  printk("  R14: 0x");
  printk_hex64(r14);
  printk("  R15: 0x");
  printk_hex64(r15);
  printk("\n");

  printk("  RFLAGS: 0x");
  printk_hex64(rflags);
  printk("\n");
}

// Dump stack contents to debug console
void platform_dump_stack(uint32_t bytes) {
  uint64_t rsp;
  __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));

  printk("\nStack dump (");
  printk_dec(bytes);
  printk(" bytes from SP=0x");
  printk_hex64(rsp);
  printk("):\n");

  uint8_t *stack = (uint8_t *)rsp;
  for (uint32_t i = 0; i < bytes; i += 16) {
    printk("  0x");
    printk_hex64(rsp + i);
    printk(": ");

    // Print 16 bytes in hex
    for (uint32_t j = 0; j < 16 && (i + j) < bytes; j++) {
      printk_hex8(stack[i + j]);
      printk(" ");
    }

    printk("\n");
  }
}

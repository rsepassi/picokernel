// RISC-V SBI (Supervisor Binary Interface)
// Implementation of SBI calls

#include "sbi.h"

// Make an SBI ecall
// Follows RISC-V SBI specification calling convention
struct sbi_ret sbi_ecall(int ext, int fid, unsigned long arg0,
                         unsigned long arg1, unsigned long arg2,
                         unsigned long arg3, unsigned long arg4,
                         unsigned long arg5) {
  struct sbi_ret ret;
  register unsigned long a0 __asm__("a0") = arg0;
  register unsigned long a1 __asm__("a1") = arg1;
  register unsigned long a2 __asm__("a2") = arg2;
  register unsigned long a3 __asm__("a3") = arg3;
  register unsigned long a4 __asm__("a4") = arg4;
  register unsigned long a5 __asm__("a5") = arg5;
  register unsigned long a6 __asm__("a6") = fid;
  register unsigned long a7 __asm__("a7") = ext;

  __asm__ volatile("ecall"
                   : "+r"(a0), "+r"(a1)
                   : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                   : "memory");

  ret.error = a0;
  ret.value = a1;
  return ret;
}

// Set timer interrupt to fire at stime_value
void sbi_set_timer(uint64_t stime_value) {
  sbi_ecall(SBI_EXT_TIME, SBI_TIMER_SET_TIMER, stime_value, 0, 0, 0, 0, 0);
}

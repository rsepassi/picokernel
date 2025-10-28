// RISC-V SBI (Supervisor Binary Interface)
// Interface for making SBI calls to M-mode firmware

#pragma once

#include <stdint.h>

// SBI Extension IDs
#define SBI_EXT_TIME 0x54494D45 // "TIME" - Timer Extension

// SBI Timer Extension Function IDs
#define SBI_TIMER_SET_TIMER 0

// SBI return structure
struct sbi_ret {
  long error;
  long value;
};

// SBI ecall function
struct sbi_ret sbi_ecall(int ext, int fid, unsigned long arg0,
                         unsigned long arg1, unsigned long arg2,
                         unsigned long arg3, unsigned long arg4,
                         unsigned long arg5);

// Timer functions
void sbi_set_timer(uint64_t stime_value);

// Read time CSR
static inline uint64_t rdtime(void) {
  uint64_t time;
  __asm__ volatile("rdtime %0" : "=r"(time));
  return time;
}

// Read cycle CSR
static inline uint64_t rdcycle(void) {
  uint64_t cycle;
  __asm__ volatile("rdcycle %0" : "=r"(cycle));
  return cycle;
}

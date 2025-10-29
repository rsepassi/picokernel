// RISC-V SBI (Supervisor Binary Interface)
// Interface for making SBI calls to M-mode firmware

#pragma once

#include <stdint.h>

// SBI Extension IDs
#define SBI_EXT_TIME 0x54494D45 // "TIME" - Timer Extension
#define SBI_EXT_SRST 0x53525354 // "SRST" - System Reset Extension

// SBI Timer Extension Function IDs
#define SBI_TIMER_SET_TIMER 0

// SBI System Reset Extension
#define SBI_SRST_SHUTDOWN 0
#define SBI_RESET_TYPE_SHUTDOWN 0
#define SBI_RESET_REASON_NO_REASON 0

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

// System reset/shutdown
static inline void sbi_shutdown(void) {
  sbi_ecall(SBI_EXT_SRST, SBI_SRST_SHUTDOWN, SBI_RESET_TYPE_SHUTDOWN,
            SBI_RESET_REASON_NO_REASON, 0, 0, 0, 0);
}

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

// RISC-V SBI (Supervisor Binary Interface)
// Interface for making SBI calls to the firmware

#pragma once

#include <stdint.h>

// SBI extension IDs
#define SBI_EXT_BASE 0x10
#define SBI_EXT_TIMER 0x54494D45 // "TIME" extension
#define SBI_EXT_IPI 0x735049
#define SBI_EXT_RFENCE 0x52464E43
#define SBI_EXT_HSM 0x48534D
#define SBI_EXT_SRST 0x53525354

// SBI function IDs for Timer extension
#define SBI_TIMER_SET_TIMER 0x0

// SBI System Reset Extension
#define SBI_SRST_SHUTDOWN 0
#define SBI_RESET_TYPE_SHUTDOWN 0
#define SBI_RESET_REASON_NO_REASON 0

// SBI return structure
typedef struct {
  long error;
  long value;
} sbi_ret_t;

// Make an SBI call with 0 arguments
sbi_ret_t sbi_ecall(long ext, long fid);

// Make an SBI call with 1 argument
sbi_ret_t sbi_ecall1(long ext, long fid, long arg0);

// Make an SBI call with 2 arguments
sbi_ret_t sbi_ecall2(long ext, long fid, long arg0, long arg1);

// Timer functions
void sbi_set_timer(uint64_t stime_value);
uint64_t sbi_get_time(void);

// System reset/shutdown
static inline void sbi_shutdown(void) {
  sbi_ecall2(SBI_EXT_SRST, SBI_SRST_SHUTDOWN, SBI_RESET_TYPE_SHUTDOWN,
             SBI_RESET_REASON_NO_REASON);
}

// RISC-V SBI (Supervisor Binary Interface)
// Implementation of SBI ecall wrappers

#include "sbi.h"

// Make an SBI ecall with up to 6 arguments
// Arguments are passed in a0-a5, extension in a7, function in a6
// Returns error in a0, value in a1
static inline sbi_ret_t sbi_ecall_impl(long ext, long fid,
                                       long arg0, long arg1,
                                       long arg2, long arg3,
                                       long arg4, long arg5)
{
    register long a0 asm("a0") = arg0;
    register long a1 asm("a1") = arg1;
    register long a2 asm("a2") = arg2;
    register long a3 asm("a3") = arg3;
    register long a4 asm("a4") = arg4;
    register long a5 asm("a5") = arg5;
    register long a6 asm("a6") = fid;
    register long a7 asm("a7") = ext;

    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");

    sbi_ret_t ret;
    ret.error = a0;
    ret.value = a1;
    return ret;
}

// Make an SBI call with 0 arguments
sbi_ret_t sbi_ecall(long ext, long fid)
{
    return sbi_ecall_impl(ext, fid, 0, 0, 0, 0, 0, 0);
}

// Make an SBI call with 1 argument
sbi_ret_t sbi_ecall1(long ext, long fid, long arg0)
{
    return sbi_ecall_impl(ext, fid, arg0, 0, 0, 0, 0, 0);
}

// Make an SBI call with 2 arguments
sbi_ret_t sbi_ecall2(long ext, long fid, long arg0, long arg1)
{
    return sbi_ecall_impl(ext, fid, arg0, arg1, 0, 0, 0, 0);
}

// Set timer to fire at stime_value
// On rv32, we need to pass the 64-bit value as two 32-bit arguments
void sbi_set_timer(uint64_t stime_value)
{
#ifdef __riscv_xlen
#if __riscv_xlen == 32
    // For RV32, pass 64-bit value in two registers
    uint32_t lo = (uint32_t)(stime_value & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(stime_value >> 32);
    sbi_ecall2(SBI_EXT_TIMER, SBI_TIMER_SET_TIMER, lo, hi);
#else
    // For RV64, pass directly
    sbi_ecall1(SBI_EXT_TIMER, SBI_TIMER_SET_TIMER, stime_value);
#endif
#else
    // Default to RV32 behavior if not defined
    uint32_t lo = (uint32_t)(stime_value & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(stime_value >> 32);
    sbi_ecall2(SBI_EXT_TIMER, SBI_TIMER_SET_TIMER, lo, hi);
#endif
}

// Read current time from time CSR
// Returns 64-bit time value (even on rv32)
uint64_t sbi_get_time(void)
{
#ifdef __riscv_xlen
#if __riscv_xlen == 32
    // For RV32, we need to read time register carefully
    // to handle overflow between low and high reads
    uint32_t hi, lo;
    do {
        asm volatile("rdtimeh %0" : "=r"(hi));
        asm volatile("rdtime %0" : "=r"(lo));
        // Read hi again to check for overflow
        uint32_t hi2;
        asm volatile("rdtimeh %0" : "=r"(hi2));
        // If hi changed, loop and try again
        if (hi == hi2) {
            break;
        }
    } while (1);

    return ((uint64_t)hi << 32) | lo;
#else
    // For RV64, single instruction
    uint64_t time;
    asm volatile("rdtime %0" : "=r"(time));
    return time;
#endif
#else
    // Default to RV32 behavior
    uint32_t hi, lo;
    do {
        asm volatile("rdtimeh %0" : "=r"(hi));
        asm volatile("rdtime %0" : "=r"(lo));
        uint32_t hi2;
        asm volatile("rdtimeh %0" : "=r"(hi2));
        if (hi == hi2) {
            break;
        }
    } while (1);

    return ((uint64_t)hi << 32) | lo;
#endif
}

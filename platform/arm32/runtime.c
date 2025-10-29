// ARM32 Runtime Library Functions
// Implements EABI runtime functions needed for division operations

#include <stdint.h>
#include <stddef.h>

// Forward declare memset from kbase.c
void *memset(void *s, int c, size_t n);

// ARM EABI function prototypes
void __aeabi_uldivmod(void);
void __aeabi_uidivmod(void);
uint32_t __aeabi_uidiv(uint32_t numerator, uint32_t denominator);
void __aeabi_memclr(void);
void __aeabi_memclr4(void);
void __aeabi_memclr8(void);

// Helper for 64-bit division - takes pointers for output
__attribute__((used)) static void do_uldivmod_helper(uint64_t numerator, uint64_t denominator,
                                                      uint64_t *quot_out, uint64_t *rem_out) {
  if (denominator == 0) {
    *quot_out = 0;
    *rem_out = 0;
    return;
  }

  uint64_t quot = 0;
  uint64_t rem = numerator;

  if (denominator > numerator) {
    *quot_out = 0;
    *rem_out = rem;
    return;
  }

  // Simple long division algorithm
  uint64_t bit = 1;
  uint64_t temp_denom = denominator;

  // Align denominator with numerator
  while (temp_denom < numerator && !(temp_denom & (1ULL << 63))) {
    temp_denom <<= 1;
    bit <<= 1;
  }

  // Perform division
  while (bit) {
    if (rem >= temp_denom) {
      rem -= temp_denom;
      quot |= bit;
    }
    temp_denom >>= 1;
    bit >>= 1;
  }

  *quot_out = quot;
  *rem_out = rem;
}

// ARM EABI 64-bit division: returns quotient in r0:r1, remainder in r2:r3
// Input: r0:r1 = numerator, r2:r3 = denominator
__attribute__((naked)) void __aeabi_uldivmod(void) {
  __asm__ volatile(
    "push {r4, r5, r6, r7, r8, lr}\n"  // Push 6 regs (24 bytes, 8-byte aligned)
    "sub sp, sp, #16\n"          // Allocate 16 bytes on stack for outputs
    // Stack layout: [sp+0..7] = quot, [sp+8..15] = rem

    // Save inputs (they're already in r0:r1, r2:r3)
    "mov r4, r0\n"               // Save numerator low
    "mov r5, r1\n"               // Save numerator high
    "mov r6, r2\n"               // Save denominator low
    "mov r7, r3\n"               // Save denominator high

    // Prepare call: do_uldivmod_helper(r0:r1=num, r2:r3=denom, [sp]=quot, [sp+8]=rem)
    "mov r0, r4\n"               // numerator low
    "mov r1, r5\n"               // numerator high
    "mov r2, r6\n"               // denominator low
    "mov r3, r7\n"               // denominator high
    "add r4, sp, #0\n"           // r4 = &quot
    "add r5, sp, #8\n"           // r5 = &rem
    "push {r4, r5}\n"            // Push pointers as stack args
    "bl do_uldivmod_helper\n"
    "add sp, sp, #8\n"           // Clean up pushed pointer args

    // Load results into return registers
    "ldrd r0, r1, [sp, #0]\n"    // r0:r1 = quot
    "ldrd r2, r3, [sp, #8]\n"    // r2:r3 = rem

    "add sp, sp, #16\n"          // Deallocate stack space
    "pop {r4, r5, r6, r7, r8, pc}\n"
  );
}

// Helper for 32-bit division
__attribute__((used)) static void do_uidivmod_helper(uint32_t numerator, uint32_t denominator,
                                                      uint32_t *quot_out, uint32_t *rem_out) {
  if (denominator == 0) {
    *quot_out = 0;
    *rem_out = 0;
    return;
  }

  uint32_t quot = 0;
  uint32_t rem = numerator;

  if (denominator > numerator) {
    *quot_out = 0;
    *rem_out = rem;
    return;
  }

  // Simple long division algorithm
  uint32_t bit = 1;
  uint32_t temp_denom = denominator;

  // Align denominator with numerator
  while (temp_denom < numerator && !(temp_denom & 0x80000000)) {
    temp_denom <<= 1;
    bit <<= 1;
  }

  // Perform division
  while (bit) {
    if (rem >= temp_denom) {
      rem -= temp_denom;
      quot |= bit;
    }
    temp_denom >>= 1;
    bit >>= 1;
  }

  *quot_out = quot;
  *rem_out = rem;
}

// ARM EABI 32-bit division: returns quotient in r0, remainder in r1
// Input: r0 = numerator, r1 = denominator
__attribute__((naked)) void __aeabi_uidivmod(void) {
  __asm__ volatile(
    "push {r4, r5, r6, lr}\n"    // Push 4 regs (16 bytes, 8-byte aligned)
    "sub sp, sp, #8\n"           // Allocate 8 bytes on stack for outputs

    // Save inputs
    "mov r4, r0\n"               // Save numerator
    "mov r5, r1\n"               // Save denominator

    // Prepare call: do_uidivmod_helper(r0=num, r1=denom, r2=&quot, r3=&rem)
    "mov r0, r4\n"               // numerator
    "mov r1, r5\n"               // denominator
    "add r2, sp, #0\n"           // r2 = &quot
    "add r3, sp, #4\n"           // r3 = &rem
    "bl do_uidivmod_helper\n"

    // Load results into return registers
    "ldr r0, [sp, #0]\n"         // r0 = quot
    "ldr r1, [sp, #4]\n"         // r1 = rem

    "add sp, sp, #8\n"           // Deallocate stack space
    "pop {r4, r5, r6, pc}\n"
  );
}

// 32-bit unsigned division (quotient only)
// Input: r0 = numerator, r1 = denominator
// Output: r0 = quotient
uint32_t __aeabi_uidiv(uint32_t numerator, uint32_t denominator) {
  uint32_t quot, rem;
  do_uidivmod_helper(numerator, denominator, &quot, &rem);
  return quot;
}

// ARM EABI memory clearing functions
// These are called by the compiler for zero-initialization

// __aeabi_memclr - clear memory (byte-aligned)
// Input: r0 = dest pointer, r1 = byte count
// Just tail-call to memset with value=0
__attribute__((naked)) void __aeabi_memclr(void) {
  __asm__ volatile(
    "mov r2, r1\n"               // r2 = count (3rd param)
    "mov r1, #0\n"               // r1 = value (2nd param) = 0
    "b memset\n"                 // Tail call to memset(r0=dest, r1=0, r2=count)
  );
}

// __aeabi_memclr4 - clear memory (4-byte aligned, count multiple of 4)
// Input: r0 = dest pointer, r1 = byte count
__attribute__((naked)) void __aeabi_memclr4(void) {
  __asm__ volatile(
    "mov r2, r1\n"               // r2 = count (3rd param)
    "mov r1, #0\n"               // r1 = value (2nd param) = 0
    "b memset\n"                 // Tail call to memset(r0=dest, r1=0, r2=count)
  );
}

// __aeabi_memclr8 - clear memory (8-byte aligned, count multiple of 8)
// Input: r0 = dest pointer, r1 = byte count
__attribute__((naked)) void __aeabi_memclr8(void) {
  __asm__ volatile(
    "mov r2, r1\n"               // r2 = count (3rd param)
    "mov r1, #0\n"               // r1 = value (2nd param) = 0
    "b memset\n"                 // Tail call to memset(r0=dest, r1=0, r2=count)
  );
}

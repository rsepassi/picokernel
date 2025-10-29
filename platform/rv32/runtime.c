// RV32 Runtime Library Functions
// Provides 64-bit division/modulo functions required by compiler

#include <stdint.h>

// Function prototypes (these are called by compiler-generated code)
uint64_t __udivdi3(uint64_t dividend, uint64_t divisor);
uint64_t __umoddi3(uint64_t dividend, uint64_t divisor);

// 64-bit unsigned division
uint64_t __udivdi3(uint64_t dividend, uint64_t divisor) {
  if (divisor == 0) {
    return 0;
  }

  uint64_t quotient = 0;
  uint64_t remainder = 0;

  // Long division algorithm
  for (int i = 63; i >= 0; i--) {
    remainder <<= 1;
    remainder |= (dividend >> i) & 1;

    if (remainder >= divisor) {
      remainder -= divisor;
      quotient |= (1ULL << i);
    }
  }

  return quotient;
}

// 64-bit unsigned modulo
uint64_t __umoddi3(uint64_t dividend, uint64_t divisor) {
  if (divisor == 0) {
    return 0;
  }

  uint64_t remainder = 0;

  // Long division algorithm (just compute remainder)
  for (int i = 63; i >= 0; i--) {
    remainder <<= 1;
    remainder |= (dividend >> i) & 1;

    if (remainder >= divisor) {
      remainder -= divisor;
    }
  }

  return remainder;
}

#ifndef KBASE_H
#define KBASE_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Container of macro - get pointer to container from member pointer */
#define KCONTAINER_OF(ptr, type, member)                                       \
  ((type *)(void *)((char *)(ptr) - offsetof(type, member)))

/* Align value up to alignment (alignment must be power of 2) */
#define KALIGN(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

/* Align value down to alignment (alignment must be power of 2) */
#define KALIGN_BACK(x, align) ((x) & ~((align) - 1))

/* Cast pointer asserting alignment */
#define KALIGN_CAST(type, ptr) ((type)(void *)(ptr))

/* Min/max of two values */
#define KMIN(a, b) (((a) < (b)) ? (a) : (b))
#define KMAX(a, b) (((a) > (b)) ? (a) : (b))

/* Array size */
#define KARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Check if value is power of 2 */
#define KIS_POWER_OF_2(x) ((x) != 0 && ((x) & ((x) - 1)) == 0)

/* Round up division */
#define KDIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

/* Bit manipulation */
#define KBIT(n) (1UL << (n))

#define KBIT_SET(val, bit) ((val) | KBIT(bit))

#define KBIT_CLEAR(val, bit) ((val) & ~KBIT(bit))

#define KBIT_TEST(val, bit) (((val) & KBIT(bit)) != 0)

/* Mark variable/parameter as unused */
#define KUNUSED(x) ((void)(x))

/* Static assertion */
#define KSTATIC_ASSERT(expr, msg) _Static_assert(expr, msg)

/* Logging macro with file and line info */
#define KSTRINGIFY(x) #x
#define KTOSTRING(x) KSTRINGIFY(x)
#define KLOG(msg)                                                              \
  do {                                                                         \
    printk("[");                                                               \
    printk(__FILE__);                                                          \
    printk(":");                                                               \
    printk(KTOSTRING(__LINE__));                                               \
    printk("] ");                                                              \
    printk(msg);                                                               \
    printk("\n");                                                              \
  } while (0)

/* Assertion macro - aborts if condition is false */
#define KASSERT(cond, msg)                                                     \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printk("\n\n=== ABORT! ===\n\n");                                        \
      printk("[");                                                             \
      printk(__FILE__);                                                        \
      printk(":");                                                             \
      printk(KTOSTRING(__LINE__));                                             \
      printk("] ASSERTION FAILED: ");                                          \
      printk(#cond);                                                           \
      printk("\n  ");                                                          \
      printk(msg);                                                             \
      printk("\n");                                                            \
      kabort();                                                                \
    }                                                                          \
  } while (0)

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

void kabort(void) __attribute__((noreturn));

#endif /* KBASE_H */

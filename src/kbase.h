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

/* Check if pointer/value is aligned to alignment (alignment must be power of 2)
 */
#define KALIGNED(ptr, align) (((uintptr_t)(ptr) & ((align) - 1)) == 0)

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

/* Endianness conversion helpers (BSD-style) */

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

/* Little-endian conversions (no-op on little-endian hosts) */
static inline uint16_t khtole16(uint16_t x) { return x; }
static inline uint32_t khtole32(uint32_t x) { return x; }
static inline uint64_t khtole64(uint64_t x) { return x; }

static inline uint16_t kle16toh(uint16_t x) { return x; }
static inline uint32_t kle32toh(uint32_t x) { return x; }
static inline uint64_t kle64toh(uint64_t x) { return x; }

/* Big-endian conversions (byte swap on little-endian hosts) */
static inline uint16_t khtobe16(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t khtobe32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint64_t khtobe64(uint64_t x) { return __builtin_bswap64(x); }

static inline uint16_t kbe16toh(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t kbe32toh(uint32_t x) { return __builtin_bswap32(x); }
static inline uint64_t kbe64toh(uint64_t x) { return __builtin_bswap64(x); }

#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

/* Little-endian conversions (byte swap on big-endian hosts) */
static inline uint16_t khtole16(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t khtole32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint64_t khtole64(uint64_t x) { return __builtin_bswap64(x); }

static inline uint16_t kle16toh(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t kle32toh(uint32_t x) { return __builtin_bswap32(x); }
static inline uint64_t kle64toh(uint64_t x) { return __builtin_bswap64(x); }

/* Big-endian conversions (no-op on big-endian hosts) */
static inline uint16_t khtobe16(uint16_t x) { return x; }
static inline uint32_t khtobe32(uint32_t x) { return x; }
static inline uint64_t khtobe64(uint64_t x) { return x; }

static inline uint16_t kbe16toh(uint16_t x) { return x; }
static inline uint32_t kbe32toh(uint32_t x) { return x; }
static inline uint64_t kbe64toh(uint64_t x) { return x; }

#else
#error "Unknown byte order"
#endif

#endif /* KBASE_H */

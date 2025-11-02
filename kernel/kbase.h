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

/* Forward declaration of time getter (defined in kernel.h) */
uint64_t kget_time_ms__logonly__(void);

#define KLOG(fmt, ...)                                                         \
  do {                                                                         \
    uint64_t _time = kget_time_ms__logonly__();                                \
    if (_time > 0) {                                                           \
      printk("[%10lu][%s:%d] " fmt "\n", (unsigned long)_time, __FILE__,       \
             __LINE__, ##__VA_ARGS__);                                         \
    } else {                                                                   \
      printk("[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);          \
    }                                                                          \
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
      kpanic(msg);                                                             \
    }                                                                          \
  } while (0)

/* Conditional debug macros - compiled out in release builds */
#ifdef KDEBUG
#define KDEBUG_ASSERT(cond, msg) KASSERT(cond, msg)
#define KDEBUG_LOG(fmt, ...) KLOG(fmt, ##__VA_ARGS__)
#define KDEBUG_VALIDATE(expr) expr
#else
#define KDEBUG_ASSERT(cond, msg) ((void)0)
#define KDEBUG_LOG(fmt, ...) ((void)0)
#define KDEBUG_VALIDATE(expr) ((void)0)
#endif

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

/* Enhanced panic handler - dumps registers, stack, and debug info */
void kpanic(const char *msg) __attribute__((noreturn));

/* Work queue debugging (KDEBUG only) */
#ifdef KDEBUG
void kdebug_dump_work_history(void);
#else
static inline void kdebug_dump_work_history(void) {}
#endif

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

/* Unaligned big-endian load helpers (for FDT/device tree parsing)
 * These perform byte-by-byte reads to avoid unaligned access faults
 * Accept volatile pointers to prevent compiler reordering */
static inline uint32_t kload_be32(const volatile uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint64_t kload_be64(const volatile uint8_t *p) {
  return ((uint64_t)kload_be32(p) << 32) | kload_be32(p + 4);
}

#endif /* KBASE_H */

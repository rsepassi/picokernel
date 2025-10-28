#ifndef KBASE_H
#define KBASE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>

/* Container of macro - get pointer to container from member pointer */
#define KCONTAINER_OF(ptr, type, member) \
    ((type *)(void*)((char *)(ptr) - offsetof(type, member)))

/* Align value up to alignment (alignment must be power of 2) */
#define KALIGN(x, align) \
    (((x) + ((align) - 1)) & ~((align) - 1))

/* Align value down to alignment (alignment must be power of 2) */
#define KALIGN_BACK(x, align) \
    ((x) & ~((align) - 1))

/* Cast pointer asserting alignment */
#define KALIGN_CAST(type, ptr) ((type)(void*)(ptr))

/* Min/max of two values */
#define KMIN(a, b) (((a) < (b)) ? (a) : (b))
#define KMAX(a, b) (((a) > (b)) ? (a) : (b))

/* Array size */
#define KARRAY_SIZE(arr) \
    (sizeof(arr) / sizeof((arr)[0]))

/* Check if value is power of 2 */
#define KIS_POWER_OF_2(x) \
    ((x) != 0 && ((x) & ((x) - 1)) == 0)

/* Round up division */
#define KDIV_ROUND_UP(n, d) \
    (((n) + (d) - 1) / (d))

/* Bit manipulation */
#define KBIT(n) \
    (1UL << (n))

#define KBIT_SET(val, bit) \
    ((val) | KBIT(bit))

#define KBIT_CLEAR(val, bit) \
    ((val) & ~KBIT(bit))

#define KBIT_TEST(val, bit) \
    (((val) & KBIT(bit)) != 0)

/* Mark variable/parameter as unused */
#define KUNUSED(x) \
    ((void)(x))

/* Static assertion */
#define KSTATIC_ASSERT(expr, msg) \
    _Static_assert(expr, msg)

#endif /* KBASE_H */

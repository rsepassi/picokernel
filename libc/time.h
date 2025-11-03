#pragma once

#include <stdint.h>

typedef int64_t time_t;

struct timespec {
  time_t tv_sec;  // Seconds
  long tv_nsec;   // Nanoseconds [0, 999999999]
};

#define NSEC_PER_SEC 1000000000ULL
#define NSEC_PER_MSEC 1000000ULL
#define NSEC_PER_USEC 1000ULL
#define USEC_PER_SEC 1000000ULL
#define MSEC_PER_SEC 1000ULL

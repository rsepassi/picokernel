// User state and entry point

#ifndef USER_H
#define USER_H

#include "kernel.h"

// User state structure
typedef struct {
  kernel_t *kernel;
  krng_req_t rng_req;
  uint8_t random_buf[32];
} kuser_t;

// User entry point
void kusermain(kuser_t *user);

#endif // USER_H

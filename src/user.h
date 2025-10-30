// User state and entry point

#ifndef USER_H
#define USER_H

#include "kernel.h"

// User state structure
typedef struct {
  kernel_t *kernel;
  krng_req_t rng_req;
  uint8_t random_buf[32];

  // Block device test state
  kblk_req_t blk_req;
  kblk_segment_t blk_segment;
  uint8_t __attribute__((aligned(4096))) sector_buffer[4096];
  int test_stage; // 0=read, 1=write, 2=verify
} kuser_t;

// User entry point
void kmain_usermain(kuser_t *user);

#endif // USER_H

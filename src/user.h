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
  int test_stage; // 0=read, 1=write, 2=verify

  // Network device test state
  knet_recv_req_t net_recv_req;
  knet_buffer_t net_rx_bufs[4];
  knet_send_req_t net_send_req;
  knet_buffer_t net_tx_packet;
  uint32_t packets_received;
  uint32_t packets_sent;

  uint8_t __attribute__((aligned(512))) sector_buffer[4096];
  uint8_t __attribute__((aligned(64))) net_rx_buf0[1514];
  uint8_t __attribute__((aligned(64))) net_rx_buf1[1514];
  uint8_t __attribute__((aligned(64))) net_rx_buf2[1514];
  uint8_t __attribute__((aligned(64))) net_rx_buf3[1514];
  uint8_t __attribute__((aligned(64))) net_tx_buf[1514];
} kuser_t;

// User entry point
void kmain_usermain(kuser_t *user);

#endif // USER_H

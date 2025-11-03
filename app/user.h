// User state and entry point

#pragma once

#include "csprng.h"
#include "kernel.h"

// User state structure
typedef struct {
  kernel_t *kernel;
  csprng_ctx rng;
  krng_req_t rng_req;
  uint8_t random_buf[32];

  // Block device test state
  kblk_req_t blk_req;
  kblk_segment_t blk_segment;
  int test_stage; // 0=read, 1=write, 2=verify

  // Network device test state
  knet_recv_req_t net_recv_req;
  knet_buffer_t net_rx_bufs[4];

  uint32_t packets_received;
  uint32_t packets_sent;

  // Per-protocol send channels to prevent cross-protocol blocking
  knet_send_req_t arp_send_req;
  knet_buffer_t arp_tx_packet;

  knet_send_req_t icmp_send_req;
  knet_buffer_t icmp_tx_packet;

  knet_send_req_t udp_send_req;
  knet_buffer_t udp_tx_packet;

  uint8_t __attribute__((aligned(512))) sector_buffer[4096];
  uint8_t __attribute__((aligned(64))) net_rx_buf0[1514];
  uint8_t __attribute__((aligned(64))) net_rx_buf1[1514];
  uint8_t __attribute__((aligned(64))) net_rx_buf2[1514];
  uint8_t __attribute__((aligned(64))) net_rx_buf3[1514];

  // Per-protocol TX buffers
  uint8_t __attribute__((aligned(64))) arp_tx_buf[64];    // 42 bytes needed
  uint8_t __attribute__((aligned(64))) icmp_tx_buf[1514]; // Full MTU
  uint8_t __attribute__((aligned(64))) udp_tx_buf[1514];  // Full MTU
} user_t;

// User entry point
void user_main(user_t *user);

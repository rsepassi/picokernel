// User entry point - test RNG and Block devices

#include "user.h"
#include "platform.h"
#include "printk.h"

// Helper to get current timestamp
static uint64_t get_timestamp_ms(kuser_t *user) {
  return user->kernel->current_time_ms;
}

// Helper to write uint64_t to buffer (little-endian)
static void write_u64(uint8_t *buf, uint64_t val) {
  buf[0] = (uint8_t)(val);
  buf[1] = (uint8_t)(val >> 8);
  buf[2] = (uint8_t)(val >> 16);
  buf[3] = (uint8_t)(val >> 24);
  buf[4] = (uint8_t)(val >> 32);
  buf[5] = (uint8_t)(val >> 40);
  buf[6] = (uint8_t)(val >> 48);
  buf[7] = (uint8_t)(val >> 56);
}

// Helper to read uint64_t from buffer (little-endian)
static uint64_t read_u64(const uint8_t *buf) {
  return (uint64_t)buf[0] | ((uint64_t)buf[1] << 8) | ((uint64_t)buf[2] << 16) |
         ((uint64_t)buf[3] << 24) | ((uint64_t)buf[4] << 32) |
         ((uint64_t)buf[5] << 40) | ((uint64_t)buf[6] << 48) |
         ((uint64_t)buf[7] << 56);
}

// RNG callback
static void on_random_ready(kwork_t *work) {
  kuser_t *user = work->ctx;
  (void)user;

  if (work->result != KERR_OK) {
    printk("RNG failed: error ");
    printk_dec(work->result);
    printk("\n");
    return;
  }

  printk("Random bytes (");
  krng_req_t *req = CONTAINER_OF(work, krng_req_t, work);
  printk_dec(req->completed);
  printk("): ");

  for (size_t i = 0; i < req->completed && i < 32; i++) {
    printk_hex8(req->buffer[i]);
    if (i < req->completed - 1)
      printk(" ");
  }
  printk("\n");
}

// Block device test callback
static void on_block_complete(kwork_t *work) {
  kuser_t *user = work->ctx;

  if (work->result != KERR_OK) {
    printk("Block operation failed: error ");
    printk_dec(work->result);
    printk("\n");
    return;
  }

  kblk_req_t *req = CONTAINER_OF(work, kblk_req_t, work);

  if (user->test_stage == 0) {
    // Stage 0: Read complete, check for magic bytes
    printk("Block read complete\n");

    uint32_t magic = (uint32_t)user->sector_buffer[0] |
                     ((uint32_t)user->sector_buffer[1] << 8) |
                     ((uint32_t)user->sector_buffer[2] << 16) |
                     ((uint32_t)user->sector_buffer[3] << 24);

    if (magic == 0x564D4F53) { // "VMOS" magic
      uint64_t timestamp = read_u64(&user->sector_buffer[4]);
      printk("Found existing magic: timestamp=");
      printk_dec(timestamp);
      printk("\n");
    } else {
      printk("No magic found, writing new magic\n");
    }

    // Stage 1: Write magic and new timestamp
    user->test_stage = 1;

    // Write magic bytes (0x564D4F53 = "VMOS")
    user->sector_buffer[0] = 0x53;
    user->sector_buffer[1] = 0x4F;
    user->sector_buffer[2] = 0x4D;
    user->sector_buffer[3] = 0x56;

    // Write current timestamp
    uint64_t now = get_timestamp_ms(user);
    write_u64(&user->sector_buffer[4], now);

    // Submit write
    req->work.op = KWORK_OP_BLOCK_WRITE;
    req->work.state = KWORK_STATE_DEAD;
    kerr_t err = ksubmit(user->kernel, &req->work);
    if (err != KERR_OK) {
      printk("Block write submit failed: error ");
      printk_dec(err);
      printk("\n");
    }
  } else if (user->test_stage == 1) {
    // Stage 1: Write complete, flush
    printk("Block write complete, flushing...\n");

    user->test_stage = 2;

    // Clear segment for flush (flush has no segments)
    req->segments = NULL;
    req->num_segments = 0;
    req->work.op = KWORK_OP_BLOCK_FLUSH;
    req->work.state = KWORK_STATE_DEAD;

    kerr_t err = ksubmit(user->kernel, &req->work);
    if (err != KERR_OK) {
      printk("Block flush submit failed: error ");
      printk_dec(err);
      printk("\n");
    }
  } else if (user->test_stage == 2) {
    // Stage 2: Flush complete, verify
    printk("Block flush complete, reading back...\n");

    user->test_stage = 3;

    // Clear buffer for verify read
    for (size_t i = 0; i < sizeof(user->sector_buffer); i++) {
      user->sector_buffer[i] = 0;
    }

    // Setup segment for read
    user->blk_segment.sector = 0;
    user->blk_segment.buffer = user->sector_buffer;
    user->blk_segment.num_sectors = 1;
    user->blk_segment.completed_sectors = 0;

    req->segments = &user->blk_segment;
    req->num_segments = 1;
    req->work.op = KWORK_OP_BLOCK_READ;
    req->work.state = KWORK_STATE_DEAD;

    kerr_t err = ksubmit(user->kernel, &req->work);
    if (err != KERR_OK) {
      printk("Block verify read submit failed: error ");
      printk_dec(err);
      printk("\n");
    }
  } else if (user->test_stage == 3) {
    // Stage 3: Verify read complete
    printk("Block verify read complete\n");

    uint32_t magic = (uint32_t)user->sector_buffer[0] |
                     ((uint32_t)user->sector_buffer[1] << 8) |
                     ((uint32_t)user->sector_buffer[2] << 16) |
                     ((uint32_t)user->sector_buffer[3] << 24);

    uint64_t timestamp = read_u64(&user->sector_buffer[4]);

    if (magic == 0x564D4F53) {
      printk("Verified magic and timestamp=");
      printk_dec(timestamp);
      printk("\n");
      printk("Block device test PASSED\n");
    } else {
      printk("Verification failed: magic mismatch\n");
      printk("Block device test FAILED\n");
    }
  }
}

// User entry point
void kmain_usermain(kuser_t *user) {
  printk("kmain_usermain: Requesting 32 random bytes...\n");

  // Setup RNG request
  user->rng_req.work.op = KWORK_OP_RNG_READ;
  user->rng_req.work.callback = on_random_ready;
  user->rng_req.work.ctx = user;
  user->rng_req.work.state = KWORK_STATE_DEAD;
  user->rng_req.work.flags = 0;
  user->rng_req.buffer = user->random_buf;
  user->rng_req.length = 32;
  user->rng_req.completed = 0;

  // Submit work
  kerr_t err = ksubmit(user->kernel, &user->rng_req.work);
  if (err != KERR_OK) {
    printk("ksubmit failed: error ");
    printk_dec(err);
    printk("\n");
  } else {
    printk("RNG request submitted\n");
  }

  // Setup block device test (stage 0: initial read)
  printk("kmain_usermain: Starting block device test...\n");

  user->test_stage = 0;

  user->blk_segment.sector = 0;
  user->blk_segment.buffer = user->sector_buffer;
  user->blk_segment.num_sectors = 1;
  user->blk_segment.completed_sectors = 0;

  user->blk_req.work.op = KWORK_OP_BLOCK_READ;
  user->blk_req.work.callback = on_block_complete;
  user->blk_req.work.ctx = user;
  user->blk_req.work.state = KWORK_STATE_DEAD;
  user->blk_req.work.flags = 0;
  user->blk_req.segments = &user->blk_segment;
  user->blk_req.num_segments = 1;

  err = ksubmit(user->kernel, &user->blk_req.work);
  if (err != KERR_OK) {
    printk("Block request submit failed: error ");
    printk_dec(err);
    printk("\n");
  } else {
    printk("Block request submitted\n");
  }
}

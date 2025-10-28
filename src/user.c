// User entry point - test RNG device

#include "user.h"
#include "printk.h"

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
}

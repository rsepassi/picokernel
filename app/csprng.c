#include "csprng.h"
#include "kbase.h"
#include "monocypher/monocypher.h"

// Initialize the CSPRNG with a seed
void csprng_init(csprng_ctx *ctx, uint8_t *seed, size_t seed_size) {
  uint8_t hash[40]; // 32 bytes for key + 8 bytes for nonce

  // Hash the seed to derive key and nonce
  crypto_blake2b(hash, sizeof(hash), seed, seed_size);

  // Extract key and nonce from hash
  memcpy(ctx->key, hash, 32);
  memcpy(ctx->nonce, hash + 32, 8);
  ctx->counter = 0;

  // Wipe the temporary hash
  crypto_wipe(hash, sizeof(hash));
  // Wipe seed buffer
  crypto_wipe(seed, seed_size);
}

// Generate random bytes using ChaCha20
void csprng_generate(csprng_ctx *ctx, uint8_t *output, size_t size) {
  // ChaCha20 is a stream cipher that XORs plaintext with keystream
  // To generate random bytes, we encrypt zeros
  // (This is equivalent to just generating the keystream)

  // Zero out the output buffer first
  memset(output, 0, size);

  // Use ChaCha20 to generate the keystream by "encrypting" zeros
  ctx->counter = crypto_chacha20_djb(output, output, size, ctx->key, ctx->nonce,
                                     ctx->counter);
}

// Mix in new entropy
void csprng_mix(csprng_ctx *ctx, const uint8_t *entropy, size_t entropy_size) {
  crypto_blake2b_ctx hash_ctx;
  uint8_t new_hash[40]; // 32 bytes for new key + 8 bytes for new nonce

  // Initialize BLAKE2b for 40-byte output
  crypto_blake2b_init(&hash_ctx, sizeof(new_hash));

  // Hash: current_key || entropy
  crypto_blake2b_update(&hash_ctx, ctx->key, 32);
  crypto_blake2b_update(&hash_ctx, entropy, entropy_size);
  crypto_blake2b_final(&hash_ctx, new_hash);

  // Update key and nonce from the hash
  memcpy(ctx->key, new_hash, 32);
  memcpy(ctx->nonce, new_hash + 32, 8);
  ctx->counter = 0; // Reset counter after rekeying

  // Wipe temporary data
  crypto_wipe(new_hash, sizeof(new_hash));
  crypto_wipe(&hash_ctx, sizeof(hash_ctx));
}

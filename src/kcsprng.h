#ifndef KCSPRNG_H
#define KCSPRNG_H

#include <stddef.h>
#include <stdint.h>

// Kernel Cryptographically Secure Pseudo-Random Number Generator
// Built on ChaCha20 (DJB variant) with BLAKE2b for entropy mixing

typedef struct {
  uint8_t key[32];  // ChaCha20 key
  uint8_t nonce[8]; // ChaCha20 nonce (DJB variant)
  uint64_t counter; // ChaCha20 counter
} kcsprng_ctx;

// Initialize the CSPRNG with a seed
// The seed is hashed with BLAKE2b to derive the initial key and nonce
void kcsprng_init(kcsprng_ctx *ctx, const uint8_t *seed, size_t seed_size);

// Generate random bytes
// Uses ChaCha20 to generate a keystream
void kcsprng_generate(kcsprng_ctx *ctx, uint8_t *output, size_t size);

// Mix in new entropy
// Combines current key with new entropy using BLAKE2b to derive a new key
// This allows forward secrecy - even if the current state is compromised,
// future states will be unpredictable given enough new entropy
void kcsprng_mix(kcsprng_ctx *ctx, const uint8_t *entropy, size_t entropy_size);

#endif // KCSPRNG_H

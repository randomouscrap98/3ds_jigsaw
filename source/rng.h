#ifndef __HEADER_JIGSAW_RNG__
#define __HEADER_JIGSAW_RNG__

#include <stdint.h>

// Tiny deterministic PRNG (xorshift32). Same seed = same sequence on every platform.
typedef struct {
  uint32_t state;
} Rng;

void rng_init(Rng * r, uint32_t seed);
uint32_t rng_next(Rng * r);
// Uniform float in [0, 1)
float rng_float(Rng * r);
// Uniform float in [lo, hi)
float rng_range(Rng * r, float lo, float hi);
// Uniform integer in [0, n). n must be > 0
uint32_t rng_below(Rng * r, uint32_t n);
// Simple string hash, useful for making seeds out of names (FNV-1a)
uint32_t rng_hashstr(const char * s);

#endif

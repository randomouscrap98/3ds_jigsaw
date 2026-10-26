#include "rng.h"

void rng_init(Rng * r, uint32_t seed) {
  // xorshift can't have a zero state; scramble the seed a bit so nearby seeds diverge
  seed ^= 0x9E3779B9u;
  seed *= 0x85EBCA6Bu;
  seed ^= seed >> 13;
  r->state = seed ? seed : 0x12345678u;
}

uint32_t rng_next(Rng * r) {
  uint32_t x = r->state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  r->state = x;
  return x;
}

float rng_float(Rng * r) {
  return (rng_next(r) >> 8) * (1.0f / 16777216.0f);
}

float rng_range(Rng * r, float lo, float hi) {
  return lo + (hi - lo) * rng_float(r);
}

uint32_t rng_below(Rng * r, uint32_t n) {
  return rng_next(r) % n;
}

uint32_t rng_hashstr(const char * s) {
  uint32_t h = 2166136261u;
  for(; *s; s++) {
    h ^= (uint8_t)*s;
    h *= 16777619u;
  }
  return h;
}

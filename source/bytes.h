#ifndef __HEADER_JIGSAW_BYTES__
#define __HEADER_JIGSAW_BYTES__

#include <stdint.h>
#include <string.h>

// Little-endian reads and writes through a moving pointer, for the save and cache formats
static inline void put16(uint8_t ** p, uint16_t v) {
  (*p)[0] = v & 0xFF; (*p)[1] = v >> 8;
  *p += 2;
}
static inline void put32(uint8_t ** p, uint32_t v) {
  for(int i = 0; i < 4; i++) (*p)[i] = (v >> (8 * i)) & 0xFF;
  *p += 4;
}
static inline void putf(uint8_t ** p, float f) {
  uint32_t v;
  memcpy(&v, &f, 4);
  put32(p, v);
}
static inline uint16_t get16(const uint8_t ** p) {
  uint16_t v = (*p)[0] | ((*p)[1] << 8);
  *p += 2;
  return v;
}
static inline uint32_t get32(const uint8_t ** p) {
  uint32_t v = 0;
  for(int i = 0; i < 4; i++) v |= (uint32_t)(*p)[i] << (8 * i);
  *p += 4;
  return v;
}
static inline float getf(const uint8_t ** p) {
  uint32_t v = get32(p);
  float f;
  memcpy(&f, &v, 4);
  return f;
}

#endif

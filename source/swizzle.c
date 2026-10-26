#include "swizzle.h"

#include <stddef.h>

uint32_t swizzle_offset(uint32_t texw, uint32_t x, uint32_t y) {
  uint32_t m = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) |
               ((x & 4) << 2) | ((y & 4) << 3);
  return (((y >> 3) * (texw >> 3) + (x >> 3)) << 6) + m;
}

void swizzle_copy(uint32_t * tex, uint32_t texw, uint32_t dx, uint32_t dy,
    const uint32_t * src, uint32_t w, uint32_t h, uint32_t srcstride) {
  for(uint32_t y = 0; y < h; y++) {
    const uint32_t * row = src + y * srcstride;
    for(uint32_t x = 0; x < w; x++) {
      tex[swizzle_offset(texw, dx + x, dy + y)] = row[x];
    }
  }
}

void swizzle_copy_rgb(uint32_t * tex, uint32_t texw, uint32_t dx, uint32_t dy,
    const uint8_t * rgb, uint32_t w, uint32_t h) {
  for(uint32_t y = 0; y < h; y++) {
    const uint8_t * row = rgb + (size_t)y * w * 3;
    for(uint32_t x = 0; x < w; x++) {
      tex[swizzle_offset(texw, dx + x, dy + y)] = SWIZZLE_RGBA(row[x * 3], row[x * 3 + 1], row[x * 3 + 2], 255);
    }
  }
}

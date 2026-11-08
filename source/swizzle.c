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

// 8 bits -> 5 with a dither offset d (0-7)
static uint32_t swizzle_to5(uint32_t v, uint32_t d) {
  v = (v + d) >> 3;
  return v > 31 ? 31 : v;
}

void swizzle_copy5551(uint16_t * tex, uint32_t texw, uint32_t dx, uint32_t dy,
    const uint32_t * src, uint32_t w, uint32_t h, uint32_t srcstride, int32_t ox, int32_t oy) {
  // Bayer 4x4 thresholds (0-15) scaled to 0-7: one 5-bit step is 8 levels of 8-bit
  static const uint8_t bayer[16] = { 0, 4, 1, 5, 6, 2, 7, 3, 1, 5, 0, 4, 7, 3, 6, 2 };
  for(uint32_t y = 0; y < h; y++) {
    const uint32_t * row = src + y * srcstride;
    uint32_t by = ((uint32_t)(oy + (int32_t)y) & 3) * 4;
    for(uint32_t x = 0; x < w; x++) {
      uint32_t p = row[x], d = bayer[by + ((uint32_t)(ox + (int32_t)x) & 3)];
      tex[swizzle_offset(texw, dx + x, dy + y)] = SWIZZLE_RGBA5551(swizzle_to5(p >> 24, d),
          swizzle_to5((p >> 16) & 0xFF, d), swizzle_to5((p >> 8) & 0xFF, d), (p & 0xFF) >= 128);
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

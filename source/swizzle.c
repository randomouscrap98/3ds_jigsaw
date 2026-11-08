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

// Bayer 4x4 thresholds (0-15)
static const uint8_t swizzle_bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

// 8 bits -> 0..mx with ordered dither threshold b (0-15): floor(v * mx / 255 + (b + 0.5) / 16), so a
// flat color averages to itself (no bias) once the GPU expands the levels back to 8 bits
static uint32_t swizzle_dither(uint32_t v, uint32_t mx, uint32_t b) {
  return (v * mx * 32 + (2 * b + 1) * 255) / (255 * 32);
}

static void swizzle_copy16(uint16_t * tex, uint32_t texw, uint32_t dx, uint32_t dy,
    const uint32_t * src, uint32_t w, uint32_t h, uint32_t srcstride, int32_t ox, int32_t oy, int four) {
  for(uint32_t y = 0; y < h; y++) {
    const uint32_t * row = src + y * srcstride;
    uint32_t by = ((uint32_t)(oy + (int32_t)y) & 3) * 4;
    for(uint32_t x = 0; x < w; x++) {
      uint32_t p = row[x], b = swizzle_bayer[by + ((uint32_t)(ox + (int32_t)x) & 3)];
      uint32_t r = p >> 24, g = (p >> 16) & 0xFF, bl = (p >> 8) & 0xFF, a = p & 0xFF;
      tex[swizzle_offset(texw, dx + x, dy + y)] = four ?
          SWIZZLE_RGBA4444(swizzle_dither(r, 15, b), swizzle_dither(g, 15, b), swizzle_dither(bl, 15, b), (a * 15 + 127) / 255) :
          SWIZZLE_RGBA5551(swizzle_dither(r, 31, b), swizzle_dither(g, 31, b), swizzle_dither(bl, 31, b), a >= 128);
    }
  }
}

void swizzle_copy5551(uint16_t * tex, uint32_t texw, uint32_t dx, uint32_t dy,
    const uint32_t * src, uint32_t w, uint32_t h, uint32_t srcstride, int32_t ox, int32_t oy) {
  swizzle_copy16(tex, texw, dx, dy, src, w, h, srcstride, ox, oy, 0);
}

void swizzle_copy4444(uint16_t * tex, uint32_t texw, uint32_t dx, uint32_t dy,
    const uint32_t * src, uint32_t w, uint32_t h, uint32_t srcstride, int32_t ox, int32_t oy) {
  swizzle_copy16(tex, texw, dx, dy, src, w, h, srcstride, ox, oy, 1);
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

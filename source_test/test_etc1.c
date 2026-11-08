#include "test.h"
#include "etc1.h"
#include "swizzle.h"
#include "rng.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// Read pixel (x, y) back out of an ETC1A4 page the way the 3DS lays it out
static void read_texel(const uint8_t * tex, uint32_t w, uint32_t x, uint32_t y, uint8_t * rgb, uint8_t * a4) {
  const uint8_t * b = tex + etc1a4_blockoffset(w, x, y);
  uint64_t alpha = 0, color = 0;
  for(int i = 0; i < 8; i++) {
    alpha |= (uint64_t)b[i] << (8 * i);
    color |= (uint64_t)b[8 + i] << (8 * i);
  }
  uint32_t lx = x & 3, ly = y & 3;
  *a4 = (alpha >> (4 * (lx * 4 + ly))) & 15;
  uint8_t px[48];
  etc1_decode(color, px);
  memcpy(rgb, px + 3 * (ly * 4 + lx), 3);
}

void test_etc1(void) {
  // Flat block: exact or within a step; two-color block split down the middle stays split
  uint8_t blk[48], dec[48];
  for(int i = 0; i < 16; i++) { blk[3 * i] = 200; blk[3 * i + 1] = 100; blk[3 * i + 2] = 50; }
  etc1_decode(etc1_encode(blk, 1), dec);
  int close = 1;
  for(int i = 0; i < 48; i++) close &= abs(dec[i] - blk[i]) <= 6;
  CHECK(close);
  for(int y = 0; y < 4; y++) for(int x = 0; x < 4; x++) {
    uint8_t v = x < 2 ? 20 : 230;
    memset(blk + 3 * (y * 4 + x), v, 3);
  }
  etc1_decode(etc1_encode(blk, 0), dec);
  int split = 1;
  for(int y = 0; y < 4; y++) for(int x = 0; x < 4; x++) split &= abs(dec[3 * (y * 4 + x)] - (x < 2 ? 20 : 230)) <= 12;
  CHECK(split);

  // Page: smooth gradient with a transparent region and a half-alpha region; read back through
  // the 3DS layout
  const uint32_t w = 32, h = 24;
  uint32_t * px = malloc(sizeof(uint32_t) * w * h);
  for(uint32_t y = 0; y < h; y++) for(uint32_t x = 0; x < w; x++) {
    uint8_t a = x >= 24 ? 0 : y >= 16 ? 128 : 255;
    px[y * w + x] = SWIZZLE_RGBA(x * 8, y * 10, 255 - x * 4, a);
  }
  uint8_t * tex = malloc(w * h);
  memset(tex, 0xAB, w * h);
  etc1a4_encode_page(px, w, h, tex, 0, NULL, "x", 0, 1);
  double se = 0;
  int n = 0, alphaok = 1, empty = 1;
  for(uint32_t y = 0; y < h; y++) for(uint32_t x = 0; x < w; x++) {
    uint8_t rgb[3], a4;
    read_texel(tex, w, x, y, rgb, &a4);
    uint32_t p = px[y * w + x];
    alphaok &= a4 == ((p & 0xFF) * 15 + 127) / 255;
    if(x >= 24) {
      const uint8_t * b = tex + etc1a4_blockoffset(w, x, y);   // Skipped block: all zero
      for(int i = 0; i < 16; i++) empty &= b[i] == 0;
      continue;
    }
    int d[3] = { rgb[0] - (int)(p >> 24), rgb[1] - (int)((p >> 16) & 0xFF), rgb[2] - (int)((p >> 8) & 0xFF) };
    se += d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    n += 3;
  }
  double db = 10 * log10(255.0 * 255.0 * n / se);
  CHECK(alphaok && empty);
  CHECK(db > 30);
  // Every block written: none of the 0xAB fill is left
  int filled = 1;
  for(uint32_t i = 0; i < w * h; i += 16) filled &= !(tex[i] == 0xAB && tex[i + 8] == 0xAB);
  CHECK(filled);
  free(px);
  free(tex);
}

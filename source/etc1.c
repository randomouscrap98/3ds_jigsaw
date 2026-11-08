#include "etc1.h"

#include <string.h>

// Modifier tables (ETC1 spec): pixel index 0 = +small, 1 = +large, 2 = -small, 3 = -large
static const int etc1_tables[8][2] = {
  { 2, 8 }, { 5, 17 }, { 9, 29 }, { 13, 42 }, { 18, 60 }, { 24, 80 }, { 33, 106 }, { 47, 183 },
};

static int etc1_mod(int table, int idx) {
  int v = etc1_tables[table][idx & 1];
  return idx & 2 ? -v : v;
}

static int etc1_clamp(int v) {
  return v < 0 ? 0 : v > 255 ? 255 : v;
}

// Pixels of subblock s (0/1) for a flip: flip 0 = left / right 2x4 halves, 1 = top / bottom 4x2
static void etc1_sub(int flip, int s, int * px) {
  int n = 0;
  for(int y = 0; y < 4; y++) {
    for(int x = 0; x < 4; x++) {
      int in = flip ? (y >> 1) == s : (x >> 1) == s;
      if(in) px[n++] = y * 4 + x;
    }
  }
}

// Perceptual-ish channel weights
#define ETC1_WR 3
#define ETC1_WG 6
#define ETC1_WB 1

// Best table and indices for 8 pixels around base color (r, g, b). Returns the weighted error.
static uint32_t etc1_fit(const uint8_t * rgb, const int * px, const int * base, int * table, uint8_t * idx) {
  uint32_t best = UINT32_MAX;
  for(int t = 0; t < 8; t++) {
    uint32_t err = 0;
    uint8_t ix[8];
    for(int i = 0; i < 8 && err < best; i++) {
      const uint8_t * p = rgb + 3 * px[i];
      uint32_t be = UINT32_MAX;
      for(int m = 0; m < 4; m++) {
        int d = etc1_mod(t, m);
        int dr = etc1_clamp(base[0] + d) - p[0], dg = etc1_clamp(base[1] + d) - p[1], db = etc1_clamp(base[2] + d) - p[2];
        uint32_t e = ETC1_WR * dr * dr + ETC1_WG * dg * dg + ETC1_WB * db * db;
        if(e < be) { be = e; ix[i] = m; }
      }
      err += be;
    }
    if(err < best) {
      best = err;
      *table = t;
      memcpy(idx, ix, 8);
    }
  }
  return best;
}

static int etc1_expand4(int v) { return v * 17; }
static int etc1_expand5(int v) { return (v << 3) | (v >> 2); }

// Quantize an average color to 4 or 5 bits per channel (rounded)
static void etc1_quant(const int * avg, int bits, int * q) {
  int mx = (1 << bits) - 1;
  for(int c = 0; c < 3; c++) {
    int v = (avg[c] * mx + 127) / 255;
    q[c] = v < 0 ? 0 : v > mx ? mx : v;
  }
}

typedef struct {
  uint32_t err;
  int diff, flip;
  int q[2][3];      // Base colors as stored: 4-bit (individual) or 5-bit (differential, q[1] absolute)
  int table[2];
  uint8_t idx[2][8];
} Etc1Try;

// One mode for a flip: quantize the averages, fit, optionally refit the bases once
static int etc1_try(const uint8_t * rgb, int flip, int diff, int quality, const int avg[2][3], Etc1Try * out) {
  int px[2][8];
  etc1_sub(flip, 0, px[0]);
  etc1_sub(flip, 1, px[1]);
  int bits = diff ? 5 : 4;
  int q[2][3];
  etc1_quant(avg[0], bits, q[0]);
  etc1_quant(avg[1], bits, q[1]);
  for(int pass = 0; pass <= quality; pass++) {
    if(diff) {
      for(int c = 0; c < 3; c++) {
        int d = q[1][c] - q[0][c];
        if(d < -4 || d > 3) return 0;
      }
    }
    uint32_t err = 0;
    Etc1Try t = { .diff = diff, .flip = flip };
    for(int s = 0; s < 2; s++) {
      int base[3];
      for(int c = 0; c < 3; c++) base[c] = diff ? etc1_expand5(q[s][c]) : etc1_expand4(q[s][c]);
      err += etc1_fit(rgb, px[s], base, &t.table[s], t.idx[s]);
      memcpy(t.q[s], q[s], sizeof(q[s]));
    }
    t.err = err;
    if(pass == 0 || err < out->err) *out = t;
    if(pass == quality) break;
    // Refit: the base that best matches the pixels minus their chosen modifiers
    int navg[2][3];
    for(int s = 0; s < 2; s++) {
      for(int c = 0; c < 3; c++) {
        int sum = 0;
        for(int i = 0; i < 8; i++) sum += rgb[3 * px[s][i] + c] - etc1_mod(t.table[s], t.idx[s][i]);
        navg[s][c] = (sum + 4) / 8;
      }
      etc1_quant(navg[s], bits, q[s]);
    }
  }
  return 1;
}

uint64_t etc1_encode(const uint8_t * rgb, int quality) {
  Etc1Try best = { .err = UINT32_MAX };
  for(int flip = 0; flip < 2; flip++) {
    int avg[2][3];
    for(int s = 0; s < 2; s++) {
      int px[8];
      etc1_sub(flip, s, px);
      for(int c = 0; c < 3; c++) {
        int sum = 0;
        for(int i = 0; i < 8; i++) sum += rgb[3 * px[i] + c];
        avg[s][c] = (sum + 4) / 8;
      }
    }
    for(int diff = 0; diff < 2; diff++) {
      Etc1Try t;
      if(etc1_try(rgb, flip, diff, quality, avg, &t) && t.err < best.err) best = t;
    }
  }
  uint64_t b = 0;
  if(best.diff) {
    for(int c = 0; c < 3; c++) {
      int d = best.q[1][c] - best.q[0][c];
      b |= (uint64_t)best.q[0][c] << (59 - 8 * c);
      b |= (uint64_t)(d & 7) << (56 - 8 * c);
    }
  } else {
    for(int c = 0; c < 3; c++) {
      b |= (uint64_t)best.q[0][c] << (60 - 8 * c);
      b |= (uint64_t)best.q[1][c] << (56 - 8 * c);
    }
  }
  b |= (uint64_t)best.table[0] << 37 | (uint64_t)best.table[1] << 34;
  b |= (uint64_t)best.diff << 33 | (uint64_t)best.flip << 32;
  // Indices: column-major pixel order (x * 4 + y), MSB plane in bits 31-16, LSB plane in 15-0
  for(int s = 0; s < 2; s++) {
    int px[8];
    etc1_sub(best.flip, s, px);
    for(int i = 0; i < 8; i++) {
      int x = px[i] % 4, y = px[i] / 4, k = x * 4 + y, m = best.idx[s][i];
      b |= (uint64_t)(m >> 1) << (16 + k);
      b |= (uint64_t)(m & 1) << k;
    }
  }
  return b;
}

void etc1_decode(uint64_t b, uint8_t * rgb) {
  int diff = (b >> 33) & 1, flip = (b >> 32) & 1;
  int base[2][3];
  for(int c = 0; c < 3; c++) {
    if(diff) {
      int q0 = (b >> (59 - 8 * c)) & 31, d = (b >> (56 - 8 * c)) & 7;
      if(d > 3) d -= 8;
      base[0][c] = etc1_expand5(q0);
      base[1][c] = etc1_expand5((q0 + d) & 31);
    } else {
      base[0][c] = etc1_expand4((b >> (60 - 8 * c)) & 15);
      base[1][c] = etc1_expand4((b >> (56 - 8 * c)) & 15);
    }
  }
  int table[2] = { (b >> 37) & 7, (b >> 34) & 7 };
  for(int y = 0; y < 4; y++) {
    for(int x = 0; x < 4; x++) {
      int s = flip ? y >> 1 : x >> 1, k = x * 4 + y;
      int m = (int)((b >> (16 + k)) & 1) << 1 | (int)((b >> k) & 1);
      int d = etc1_mod(table[s], m);
      for(int c = 0; c < 3; c++) rgb[3 * (y * 4 + x) + c] = etc1_clamp(base[s][c] + d);
    }
  }
}

uint32_t etc1a4_blockoffset(uint32_t w, uint32_t x, uint32_t y) {
  return ((y >> 3) * (w >> 3) + (x >> 3)) * 64 + (((x >> 2) & 1) + 2 * ((y >> 2) & 1)) * 16;
}

static void etc1_put64(uint8_t * p, uint64_t v) {
  for(int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

void etc1a4_encode_page(const uint32_t * rgba, uint32_t w, uint32_t h, uint8_t * out, int quality,
    const Progress * pr, const char * msg, int done0, int total) {
  for(uint32_t by = 0; by < h; by += 4) {
    if(by % 8 == 0) progress_report(pr, msg, done0 + (int)(by / 8), total);
    for(uint32_t bx = 0; bx < w; bx += 4) {
      uint8_t * o = out + etc1a4_blockoffset(w, bx, by);
      uint8_t rgb[48];
      uint64_t alpha = 0;
      for(int y = 0; y < 4; y++) {
        for(int x = 0; x < 4; x++) {
          uint32_t p = rgba[(by + y) * w + bx + x];
          uint64_t a4 = ((p & 0xFF) * 15 + 127) / 255;
          alpha |= a4 << (4 * (x * 4 + y));
          uint8_t * c = rgb + 3 * (y * 4 + x);
          c[0] = p >> 24;
          c[1] = (p >> 16) & 0xFF;
          c[2] = (p >> 8) & 0xFF;
        }
      }
      etc1_put64(o, alpha);
      etc1_put64(o + 8, alpha ? etc1_encode(rgb, quality) : 0);
    }
  }
}

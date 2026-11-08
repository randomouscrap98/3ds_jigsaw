// Picture quality of the piece texture options, side by side at the same display size:
// Standard (1024 px, RGBA8), Alt High (1448 px, RGBA5551 + ordered dither), Super (2048 px, ETC1).
// Usage: etc1_preview IMAGE OUTDIR [cx cy cw]  (crop: center and width as fractions of the
// picture; height follows the output aspect). Writes crop_standard/althigh/super.png and
// sheet.png (the three left to right), and prints error and ETC1 encode speed.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "etc1.h"
#include "imgscale.h"
#include "swizzle.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OUTW 400
#define OUTH 300
#define GAP 8

static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

// Whole picture scaled to fit cap x cap
static uint8_t * scaled(const uint8_t * src, int sw, int sh, int cap, int * w, int * h) {
  uint16_t fw, fh;
  imgscale_fit(sw, sh, cap, cap, &fw, &fh);
  uint8_t * out = malloc((size_t)fw * fh * 3);
  imgscale_area(src, sw, sh, 3, 0, 0, sw, sh, out, fw, fh);
  *w = fw;
  *h = fh;
  return out;
}

static double psnr(const uint8_t * a, const uint8_t * b, size_t n) {
  double se = 0;
  for(size_t i = 0; i < n; i++) se += (double)(a[i] - b[i]) * (a[i] - b[i]);
  return se == 0 ? 99 : 10 * log10(255.0 * 255.0 * n / se);
}

// RGBA5551 with the app's dither, read back as 8-bit RGB (5 -> 8 bits as the GPU expands)
static uint8_t * althigh(const uint8_t * rgb, int w, int h) {
  int tw = (w + 7) & ~7, th = (h + 7) & ~7;
  uint32_t * px = malloc(sizeof(uint32_t) * w * h);
  for(int i = 0; i < w * h; i++) px[i] = SWIZZLE_RGBA(rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2], 255);
  uint16_t * tex = calloc((size_t)tw * th, 2);
  swizzle_copy5551(tex, tw, 0, 0, px, w, h, w, 0, 0);
  uint8_t * out = malloc((size_t)w * h * 3);
  for(int y = 0; y < h; y++) for(int x = 0; x < w; x++) {
    uint16_t v = tex[swizzle_offset(tw, x, y)];
    int r = v >> 11, g = (v >> 6) & 31, b = (v >> 1) & 31;
    uint8_t * o = out + 3 * (y * w + x);
    o[0] = (r << 3) | (r >> 2); o[1] = (g << 3) | (g >> 2); o[2] = (b << 3) | (b >> 2);
  }
  free(px);
  free(tex);
  return out;
}

// ETC1 round trip (edge blocks padded by repeating the last row/column). Returns seconds spent encoding.
static double etc1_roundtrip(const uint8_t * rgb, int w, int h, int quality, uint8_t * out) {
  double t = 0;
  for(int by = 0; by < h; by += 4) for(int bx = 0; bx < w; bx += 4) {
    uint8_t blk[48], dec[48];
    for(int y = 0; y < 4; y++) for(int x = 0; x < 4; x++) {
      int sx = bx + x < w ? bx + x : w - 1, sy = by + y < h ? by + y : h - 1;
      memcpy(blk + 3 * (y * 4 + x), rgb + 3 * (sy * w + sx), 3);
    }
    double t0 = now();
    uint64_t b = etc1_encode(blk, quality);
    t += now() - t0;
    etc1_decode(b, dec);
    for(int y = 0; y < 4 && by + y < h; y++) for(int x = 0; x < 4 && bx + x < w; x++) {
      memcpy(out + 3 * ((by + y) * w + bx + x), dec + 3 * (y * 4 + x), 3);
    }
  }
  return t;
}

// Bilinear sample of the crop (fractions of the picture) into OUTW x OUTH, as the GPU filters
static void crop(const uint8_t * img, int w, int h, double cx, double cy, double cw, uint8_t * out) {
  double ch = cw * w / h * OUTH / OUTW;
  for(int oy = 0; oy < OUTH; oy++) for(int ox = 0; ox < OUTW; ox++) {
    double fx = ((cx - cw / 2) + cw * (ox + 0.5) / OUTW) * w - 0.5;
    double fy = ((cy - ch / 2) + ch * (oy + 0.5) / OUTH) * h - 0.5;
    int x0 = (int)floor(fx), y0 = (int)floor(fy);
    double ax = fx - x0, ay = fy - y0;
    for(int c = 0; c < 3; c++) {
      double v = 0;
      for(int k = 0; k < 4; k++) {
        int x = x0 + (k & 1), y = y0 + (k >> 1);
        x = x < 0 ? 0 : x >= w ? w - 1 : x;
        y = y < 0 ? 0 : y >= h ? h - 1 : y;
        v += img[3 * (y * w + x) + c] * ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay);
      }
      out[3 * (oy * OUTW + ox) + c] = (uint8_t)lround(v);
    }
  }
}

int main(int argc, char ** argv) {
  if(argc < 3) {
    fprintf(stderr, "usage: %s IMAGE OUTDIR [cx cy cw]\n", argv[0]);
    return 1;
  }
  double cx = argc > 4 ? atof(argv[3]) : 0.5, cy = argc > 4 ? atof(argv[4]) : 0.5, cw = argc > 5 ? atof(argv[5]) : 0.12;
  int sw, sh, comp;
  uint8_t * src = stbi_load(argv[1], &sw, &sh, &comp, 3);
  if(!src) { fprintf(stderr, "can't load %s\n", argv[1]); return 1; }
  int w1, h1, w2, h2, w3, h3;
  uint8_t * std = scaled(src, sw, sh, 1024, &w1, &h1);
  uint8_t * hi = scaled(src, sw, sh, 1448, &w2, &h2);
  uint8_t * sup = scaled(src, sw, sh, 2048, &w3, &h3);
  uint8_t * hiq = althigh(hi, w2, h2);
  uint8_t * supq = malloc((size_t)w3 * h3 * 3);
  double t0 = etc1_roundtrip(sup, w3, h3, 0, supq);
  double p0 = psnr(sup, supq, (size_t)w3 * h3 * 3);
  double t1 = etc1_roundtrip(sup, w3, h3, 1, supq);
  double p1 = psnr(sup, supq, (size_t)w3 * h3 * 3);
  double mpx = w3 * (double)h3 / 1e6;
  printf("sizes: standard %dx%d, alt high %dx%d, super %dx%d\n", w1, h1, w2, h2, w3, h3);
  printf("2000 pieces (52 columns at 4:3): cell %.1f / %.1f / %.1f px\n", w1 / 52.0, w2 / 52.0, w3 / 52.0);
  printf("alt high RGBA5551 + dither vs its 1448 input: %.1f dB\n", psnr(hi, hiq, (size_t)w2 * h2 * 3));
  printf("super ETC1 vs its 2048 input: fast %.1f dB (%.2f s, %.1f Mpx/s), refit %.1f dB (%.2f s, %.1f Mpx/s) (host, 1 core)\n",
      p0, t0, mpx / t0, p1, t1, mpx / t1);
  uint8_t * c[3];
  const char * names[3] = { "crop_standard.png", "crop_althigh.png", "crop_super.png" };
  const uint8_t * imgs[3] = { std, hiq, supq };
  int ws[3] = { w1, w2, w3 }, hs[3] = { h1, h2, h3 };
  int sheetw = 3 * OUTW + 2 * GAP;
  uint8_t * sheet = calloc((size_t)sheetw * OUTH * 3, 1);
  char path[1024];
  for(int i = 0; i < 3; i++) {
    c[i] = malloc(OUTW * OUTH * 3);
    crop(imgs[i], ws[i], hs[i], cx, cy, cw, c[i]);
    snprintf(path, sizeof(path), "%s/%s", argv[2], names[i]);
    stbi_write_png(path, OUTW, OUTH, 3, c[i], OUTW * 3);
    for(int y = 0; y < OUTH; y++) memcpy(sheet + 3 * (y * sheetw + i * (OUTW + GAP)), c[i] + 3 * y * OUTW, OUTW * 3);
  }
  snprintf(path, sizeof(path), "%s/sheet.png", argv[2]);
  stbi_write_png(path, sheetw, OUTH, 3, sheet, sheetw * 3);
  return 0;
}

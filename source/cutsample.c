#include "cutsample.h"
#include "cutter.h"
#include "layout.h"
#include "swizzle.h"

#include <stdlib.h>
#include <math.h>

// Lower each pixel near segment ab to its distance from it
static void cutsample_segment(float * dist, uint16_t w, uint16_t h, float ax, float ay, float bx, float by) {
  float r = CUTSAMPLE_HALO + 1;
  int x0 = (int)floorf(fminf(ax, bx) - r), x1 = (int)ceilf(fmaxf(ax, bx) + r);
  int y0 = (int)floorf(fminf(ay, by) - r), y1 = (int)ceilf(fmaxf(ay, by) + r);
  if(x0 < 0) x0 = 0;
  if(y0 < 0) y0 = 0;
  if(x1 > w - 1) x1 = w - 1;
  if(y1 > h - 1) y1 = h - 1;
  float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
  for(int y = y0; y <= y1; y++) {
    for(int x = x0; x <= x1; x++) {
      float px = x + 0.5f - ax, py = y + 0.5f - ay;
      float t = len2 > 0 ? (px * dx + py * dy) / len2 : 0;
      if(t < 0) t = 0;
      if(t > 1) t = 1;
      float ex = px - t * dx, ey = py - t * dy;
      float d = sqrtf(ex * ex + ey * ey);
      float * o = dist + (size_t)y * w + x;
      if(d < *o) *o = d;
    }
  }
}

static float cutsample_clamp01(float v) {
  return v < 0 ? 0 : v > 1 ? 1 : v;
}

void cutsample_crop(uint16_t w, uint16_t h, uint32_t target, uint16_t * x, uint16_t * y, uint16_t * cw, uint16_t * ch) {
  Layout l;
  if(layout_crop(&l, w, h, target)) {
    *x = *y = 0;
    *cw = w;
    *ch = h;
    return;
  }
  *cw = (uint16_t)(l.cropw + 0.5f);
  *ch = (uint16_t)(l.croph + 0.5f);
  if(*cw > w) *cw = w;
  if(*ch > h) *ch = h;
  if(*cw < 1) *cw = 1;
  if(*ch < 1) *ch = 1;
  *x = (w - *cw) / 2;
  *y = (h - *ch) / 2;
}

int cutsample_draw(uint8_t style, uint8_t whimsy, uint8_t set, uint16_t w, uint16_t h, uint32_t * px) {
  if(!w || !h) return 1;
  uint16_t cols, rows;
  layout_grid(w, h, style == CUT_STYLE_MAZE ? CUTSAMPLE_MAZEPIECES : CUTSAMPLE_PIECES, &cols, &rows);
  Cutter c;
  if(cutter_init(&c, style, whimsy, set, cols, rows, CUTSAMPLE_SEED)) return 1;
  float * dist = malloc(sizeof(float) * w * h);
  float * xy = malloc(sizeof(float) * 2 * c.maxpoints);
  int err = !dist || !xy;
  if(!err) {
    for(size_t i = 0; i < (size_t)w * h; i++) dist[i] = 1e9f;
    // Stretched to fill the image exactly (a few percent at most), so the border is its edge
    float sx = (float)w / cols, sy = (float)h / rows;
    for(uint32_t p = 0; p < c.count; p++) {
      uint32_t n = cutter_outline(&c, p, 1, 0, 0, xy);
      for(uint32_t i = 0; i < n; i++) {
        xy[2 * i] *= sx;
        xy[2 * i + 1] *= sy;
      }
      for(uint32_t i = 0; i < n; i++) {
        uint32_t j = (i + 1) % n;
        cutsample_segment(dist, w, h, xy[2 * i], xy[2 * i + 1], xy[2 * j], xy[2 * j + 1]);
      }
    }
    // Dark line over a light halo, both antialiased by distance
    const float lc[3] = { CUTSAMPLE_LINECOLOR }, hc[3] = { CUTSAMPLE_HALOCOLOR };
    for(size_t i = 0; i < (size_t)w * h; i++) {
      float la = cutsample_clamp01(CUTSAMPLE_LINE + 0.5f - dist[i]) * (CUTSAMPLE_LINEALPHA / 255.0f);
      float ha = cutsample_clamp01(CUTSAMPLE_HALO + 0.5f - dist[i]) * (CUTSAMPLE_HALOALPHA / 255.0f);
      float a = la + (1 - la) * ha;
      if(a <= 0) {
        px[i] = 0;
        continue;
      }
      float col[3];
      for(int k = 0; k < 3; k++) col[k] = (la * lc[k] + (1 - la) * ha * hc[k]) / a;
      px[i] = SWIZZLE_RGBA((uint8_t)(col[0] + 0.5f), (uint8_t)(col[1] + 0.5f), (uint8_t)(col[2] + 0.5f),
          (uint8_t)(a * 255 + 0.5f));
    }
  }
  free(xy);
  free(dist);
  cutter_free(&c);
  return err;
}

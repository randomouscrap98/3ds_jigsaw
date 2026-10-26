#include "piecebake.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

int piecebake_init(PieceBaker * pb, uint16_t tile) {
  pb->tile = tile;
  pb->cover = malloc(sizeof(float) * tile * tile);
  return pb->cover ? 0 : 1;
}

void piecebake_free(PieceBaker * pb) {
  free(pb->cover);
  pb->cover = NULL;
}

// Even-odd scanline fill with PIECEBAKE_SUBROWS samples per pixel row and exact
// horizontal coverage.
static void piecebake_cover(PieceBaker * pb, const float * xy, int npts) {
  int tile = pb->tile;
  memset(pb->cover, 0, sizeof(float) * tile * tile);
  float cross[PIECEBAKE_MAXCROSS];
  const float weight = 1.0f / PIECEBAKE_SUBROWS;
  for(int py = 0; py < tile; py++) {
    float * row = pb->cover + py * tile;
    for(int s = 0; s < PIECEBAKE_SUBROWS; s++) {
      float sy = py + (s + 0.5f) * weight;
      int nc = 0;
      for(int i = 0; i < npts; i++) {
        int j = (i + 1) % npts;
        float x0 = xy[i * 2], y0 = xy[i * 2 + 1], x1 = xy[j * 2], y1 = xy[j * 2 + 1];
        if((y0 <= sy && sy < y1) || (y1 <= sy && sy < y0)) {
          if(nc < PIECEBAKE_MAXCROSS) {
            cross[nc++] = x0 + (sy - y0) * (x1 - x0) / (y1 - y0);
          }
        }
      }
      // Insertion sort, there are only a handful of crossings
      for(int i = 1; i < nc; i++) {
        float v = cross[i];
        int j = i - 1;
        while(j >= 0 && cross[j] > v) { cross[j + 1] = cross[j]; j--; }
        cross[j + 1] = v;
      }
      for(int i = 0; i + 1 < nc; i += 2) {
        float xa = cross[i] < 0 ? 0 : cross[i];
        float xb = cross[i + 1] > tile ? tile : cross[i + 1];
        if(xb <= xa) continue;
        int ia = (int)xa, ib = (int)xb;
        if(ib >= tile) ib = tile - 1;
        for(int ix = ia; ix <= ib; ix++) {
          float lo = ix > xa ? ix : xa;
          float hi = ix + 1 < xb ? ix + 1 : xb;
          if(hi > lo) row[ix] += (hi - lo) * weight;
        }
      }
    }
  }
}

void piecebake_tile(PieceBaker * pb, const float * outline, int npts,
    const uint8_t * img, uint32_t imgw, uint32_t imgh, int32_t ox, int32_t oy, uint32_t * out) {
  piecebake_cover(pb, outline, npts);
  int tile = pb->tile;
  for(int y = 0; y < tile; y++) {
    int32_t iy = oy + y;
    if(iy < 0) iy = 0;
    if(iy >= (int32_t)imgh) iy = imgh - 1;
    for(int x = 0; x < tile; x++) {
      int32_t ix = ox + x;
      if(ix < 0) ix = 0;
      if(ix >= (int32_t)imgw) ix = imgw - 1;
      const uint8_t * p = img + ((size_t)iy * imgw + ix) * 3;
      float c = pb->cover[y * tile + x];
      if(c > 1) c = 1;
      // Outline: how much of this pixel is near the boundary = coverage minus the
      // smallest coverage around it (outside the tile counts as empty)
      float inner = c;
      for(int dy = -1; dy <= 1; dy++) {
        for(int dx = -1; dx <= 1; dx++) {
          int nx = x + dx, ny = y + dy;
          float n = (nx < 0 || ny < 0 || nx >= tile || ny >= tile) ? 0 : pb->cover[ny * tile + nx];
          if(n < inner) inner = n;
        }
      }
      float shade = 1 - PIECEBAKE_OUTLINE * (c - inner);
      out[y * tile + x] = PIECEBAKE_RGBA(p[0] * shade, p[1] * shade, p[2] * shade,
          (uint8_t)(c * 255 + 0.5f));
    }
  }
}

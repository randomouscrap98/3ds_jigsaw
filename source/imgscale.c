#include "imgscale.h"

#include <stdlib.h>
#include <math.h>

// Weighted span of source samples [start, end) along one axis. Calls back per sample.
#define IMGSCALE_SPAN(start, end, limit, ...) { \
  int _i0 = (int)floorf(start), _i1 = (int)ceilf(end); \
  for(int _i = _i0; _i < _i1; _i++) { \
    float _lo = _i > (start) ? _i : (start); \
    float _hi = _i + 1 < (end) ? _i + 1 : (end); \
    float w = _hi - _lo; \
    if(w <= 0) continue; \
    int i = _i < 0 ? 0 : (_i >= (int)(limit) ? (int)(limit) - 1 : _i); \
    __VA_ARGS__ \
  } \
}

int imgscale_area(const uint8_t * src, uint32_t srcw, uint32_t srch, int comp,
    float sx, float sy, float sw, float sh, uint8_t * dst, uint32_t dw, uint32_t dh) {
  if(comp < 1 || comp > 4 || dw == 0 || dh == 0) return 1;
  // Streams one output row at a time: each source row in the row's vertical span is
  // resampled horizontally and accumulated. Memory is just a couple of output rows.
  float * row = malloc(sizeof(float) * dw * comp);
  float * acc = malloc(sizeof(float) * dw * comp);
  if(!row || !acc) {
    free(row);
    free(acc);
    return 3;
  }
  float fx = sw / dw, fy = sh / dh;
  for(uint32_t y = 0; y < dh; y++) {
    float ystart = sy + y * fy, yend = ystart + fy, wtotal = 0;
    for(size_t k = 0; k < (size_t)dw * comp; k++) acc[k] = 0;
    IMGSCALE_SPAN(ystart, yend, srch, {
      float wy = w;
      const uint8_t * srow = src + (size_t)i * srcw * comp;
      for(uint32_t x = 0; x < dw; x++) {
        float xstart = sx + x * fx, xend = xstart + fx;
        float px[4] = {0}, wsum = 0;
        IMGSCALE_SPAN(xstart, xend, srcw, {
          for(int c = 0; c < comp; c++) px[c] += w * srow[i * comp + c];
          wsum += w;
        });
        for(int c = 0; c < comp; c++) row[x * comp + c] = wsum > 0 ? px[c] / wsum : 0;
      }
      for(size_t k = 0; k < (size_t)dw * comp; k++) acc[k] += wy * row[k];
      wtotal += wy;
    });
    uint8_t * drow = dst + (size_t)y * dw * comp;
    for(size_t k = 0; k < (size_t)dw * comp; k++) {
      float v = wtotal > 0 ? acc[k] / wtotal : 0;
      drow[k] = (uint8_t)(v + 0.5f);
    }
  }
  free(row);
  free(acc);
  return 0;
}

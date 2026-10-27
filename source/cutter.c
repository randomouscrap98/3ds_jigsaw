#include "cutter.h"

#include <stdlib.h>
#include <string.h>

int cutter_init(Cutter * c, uint8_t style, uint16_t cols, uint16_t rows, uint32_t seed) {
  memset(c, 0, sizeof(Cutter));
  c->style = style < CUT_STYLES ? style : CUT_STYLE_CLASSIC;
  c->cols = cols;
  c->rows = rows;
  c->count = (uint32_t)cols * rows;
  if(c->style == CUT_STYLE_RANDOM) {
    if(randcut_init(&c->rc, cols, rows, c->count, seed, randcut_style(RANDCUT_STYLE_EVEN))) return 1;
    c->maxpoints = randcut_maxpoints(&c->rc);
  } else {
    if(edgegen_init(&c->eg, cols, rows, seed, edgegen_style(c->style))) return 1;
    c->maxpoints = EDGEGEN_MAXPOINTS;
  }
  return 0;
}

void cutter_free(Cutter * c) {
  if(c->style == CUT_STYLE_RANDOM) randcut_free(&c->rc);
  else edgegen_free(&c->eg);
  memset(c, 0, sizeof(Cutter));
}

uint32_t cutter_outline(const Cutter * c, uint32_t p, float cell, float ox, float oy, float * xy) {
  if(c->style == CUT_STYLE_RANDOM) return randcut_outline(&c->rc, p, cell, ox, oy, xy);
  uint16_t col = p % c->cols, row = p / c->cols;
  return edgegen_outline(&c->eg, col, row, cell, ox + col * cell, oy + row * cell, xy);
}

int cutter_bounds(const Cutter * c, float * boxes) {
  float * xy = malloc(sizeof(float) * 2 * c->maxpoints);
  if(!xy) return 1;
  for(uint32_t p = 0; p < c->count; p++) {
    uint32_t n = cutter_outline(c, p, 1, 0, 0, xy);
    float * b = boxes + 4 * p;
    b[0] = b[2] = xy[0];
    b[1] = b[3] = xy[1];
    for(uint32_t i = 1; i < n; i++) {
      if(xy[i * 2] < b[0]) b[0] = xy[i * 2];
      if(xy[i * 2] > b[2]) b[2] = xy[i * 2];
      if(xy[i * 2 + 1] < b[1]) b[1] = xy[i * 2 + 1];
      if(xy[i * 2 + 1] > b[3]) b[3] = xy[i * 2 + 1];
    }
  }
  free(xy);
  return 0;
}

int cutter_geo(const Cutter * c, float cell, PieceGeo * g) {
  if(c->style != CUT_STYLE_RANDOM) return piecegeo_grid(g, c->cols, c->rows, cell, c->eg.corners);
  if(randcut_geo(&c->rc, cell, g)) return 1;
  g->cols = c->cols;
  g->rows = c->rows;
  return 0;
}

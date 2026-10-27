#include "layout.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void layout_grid(uint32_t srcw, uint32_t srch, uint32_t target, uint16_t * cols, uint16_t * rows) {
  float aspect = (float)srcw / srch;
  float best = 1e30f;
  *cols = 1;
  *rows = 1;
  for(uint32_t r = 1; r <= target; r++) {
    float ideal = r * aspect;
    uint32_t cands[2] = { (uint32_t)floorf(ideal), (uint32_t)ceilf(ideal) };
    for(int i = 0; i < 2; i++) {
      uint32_t c = cands[i];
      if(c < 1) continue;
      float score = fabsf((float)(c * r) - target) / target + 0.5f * fabsf(logf(c / ideal));
      if(score < best) {
        best = score;
        *cols = c;
        *rows = r;
      }
    }
  }
}

int layout_crop(Layout * l, uint32_t srcw, uint32_t srch, uint32_t target) {
  memset(l, 0, sizeof(Layout));
  if(srcw == 0 || srch == 0 || target == 0) return 1;
  layout_grid(srcw, srch, target, &l->cols, &l->rows);
  float gaspect = (float)l->cols / l->rows;
  l->cropw = srcw;
  l->croph = srcw / gaspect;
  if(l->croph > srch) {
    l->croph = srch;
    l->cropw = srch * gaspect;
  }
  l->cropx = (srcw - l->cropw) / 2;
  l->cropy = (srch - l->croph) / 2;
  return 0;
}

typedef struct {
  uint16_t h;
  uint32_t i;
} LayoutOrder;

static int layout_ordercmp(const void * a, const void * b) {
  const LayoutOrder * x = a, * y = b;
  if(x->h != y->h) return x->h > y->h ? -1 : 1;
  return x->i < y->i ? -1 : x->i > y->i;
}

// Tiles at this cell size; then shelf-pack them tallest first. Returns 1 if they fit.
static int layout_pack(uint16_t cell, const float * boxes, uint32_t count, uint32_t maxpages,
    LayoutTile * tiles, LayoutOrder * order, uint16_t * pages) {
  for(uint32_t p = 0; p < count; p++) {
    const float * b = boxes + 4 * p;
    LayoutTile * t = tiles + p;
    t->ox = (int32_t)floorf(b[0] * cell) - LAYOUT_PAD;
    t->oy = (int32_t)floorf(b[1] * cell) - LAYOUT_PAD;
    int32_t w = (int32_t)ceilf(b[2] * cell) + LAYOUT_PAD - t->ox;
    int32_t h = (int32_t)ceilf(b[3] * cell) + LAYOUT_PAD - t->oy;
    if(w > LAYOUT_PAGESIZE || h > LAYOUT_PAGESIZE) return 0;
    t->w = w;
    t->h = h;
    order[p] = (LayoutOrder) { t->h, p };
  }
  qsort(order, count, sizeof(LayoutOrder), layout_ordercmp);
  uint32_t page = 0, x = 0, y = 0, shelf = 0;
  for(uint32_t k = 0; k < count; k++) {
    LayoutTile * t = tiles + order[k].i;
    if(x + t->w > LAYOUT_PAGESIZE) {
      y += shelf;
      x = 0;
      shelf = 0;
    }
    if(y + t->h > LAYOUT_PAGESIZE) {
      page++;
      x = y = 0;
      shelf = 0;
    }
    if(page >= maxpages) return 0;
    t->page = page;
    t->x = x;
    t->y = y;
    x += t->w;
    if(t->h > shelf) shelf = t->h;
  }
  *pages = count ? page + 1 : 0;
  return 1;
}

int layout_fit(Layout * l, const float * boxes, uint32_t count, uint32_t maxside, uint32_t maxpages,
    LayoutTile * tiles) {
  LayoutOrder * order = malloc(sizeof(LayoutOrder) * (count ? count : 1));
  if(!order) return 1;
  float fw = l->cropw < maxside ? l->cropw : maxside;
  float fh = l->croph < maxside ? l->croph : maxside;
  float fc = fw / l->cols < fh / l->rows ? fw / l->cols : fh / l->rows;
  int cell = (int)floorf(fc);
  for(; cell >= LAYOUT_MINCELL; cell--) {
    if(layout_pack(cell, boxes, count, maxpages, tiles, order, &l->pages)) break;
  }
  free(order);
  if(cell < LAYOUT_MINCELL) return 2;
  l->cell = cell;
  l->width = (uint32_t)l->cols * cell;
  l->height = (uint32_t)l->rows * cell;
  l->maxw = l->maxh = 0;
  for(uint32_t p = 0; p < count; p++) {
    if(tiles[p].w > l->maxw) l->maxw = tiles[p].w;
    if(tiles[p].h > l->maxh) l->maxh = tiles[p].h;
  }
  return 0;
}

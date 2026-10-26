#include "layout.h"

#include <math.h>

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

static void layout_pack(Layout * l, uint16_t cell) {
  l->cell = cell;
  l->margin = LAYOUT_MARGIN(cell);
  l->tile = cell + 2 * l->margin;
  l->perrow = LAYOUT_PAGESIZE / l->tile;
  l->perpage = l->perrow * l->perrow;
  uint32_t count = (uint32_t)l->cols * l->rows;
  l->pages = l->perpage ? (count + l->perpage - 1) / l->perpage : 0xFFFF;
}

int layout_compute(Layout * l, uint32_t srcw, uint32_t srch, uint32_t target,
    uint32_t maxside, uint32_t maxpages) {
  if(srcw == 0 || srch == 0 || target == 0) return 1;
  layout_grid(srcw, srch, target, &l->cols, &l->rows);
  // Centered crop to the grid's exact aspect
  float gaspect = (float)l->cols / l->rows;
  l->cropw = srcw;
  l->croph = srcw / gaspect;
  if(l->croph > srch) {
    l->croph = srch;
    l->cropw = srch * gaspect;
  }
  l->cropx = (srcw - l->cropw) / 2;
  l->cropy = (srch - l->croph) / 2;
  float fw = l->cropw < maxside ? l->cropw : maxside;
  float fh = l->croph < maxside ? l->croph : maxside;
  float fc = fw / l->cols < fh / l->rows ? fw / l->cols : fh / l->rows;
  int cell = (int)floorf(fc);
  for(; cell >= LAYOUT_MINCELL; cell--) {
    layout_pack(l, cell);
    if(l->perrow > 0 && l->pages <= maxpages) break;
  }
  if(cell < LAYOUT_MINCELL) return 2;
  l->width = (uint32_t)l->cols * l->cell;
  l->height = (uint32_t)l->rows * l->cell;
  return 0;
}

void layout_tile(const Layout * l, uint32_t piece, uint16_t * page, uint16_t * x, uint16_t * y) {
  *page = piece / l->perpage;
  uint32_t i = piece % l->perpage;
  *x = (i % l->perrow) * l->tile;
  *y = (i / l->perrow) * l->tile;
}

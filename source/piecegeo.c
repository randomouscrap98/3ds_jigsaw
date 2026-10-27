#include "piecegeo.h"

#include "bytes.h"
#include "geom.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PIECEGEO_HEADER 34
#define PIECEGEO_MAXSQUARES 4096   // Lookup grid squares per side (sanity limit on read)

void piecegeo_free(PieceGeo * g) {
  free(g->center);
  free(g->nbstart);
  free(g->nb);
  free(g->hbstart);
  free(g->hb);
  free(g->lstart);
  free(g->lpiece);
  memset(g, 0, sizeof(PieceGeo));
}

// Lookup squares covered by a piece's hitbox bounds (clamped to the grid)
static void piecegeo_span(const PieceGeo * g, uint32_t p, int32_t * x0, int32_t * y0, int32_t * x1, int32_t * y1) {
  const float * h = g->hb + 2 * g->hbstart[p];
  uint32_t n = g->hbstart[p + 1] - g->hbstart[p];
  float bx0 = h[0], by0 = h[1], bx1 = h[0], by1 = h[1];
  for(uint32_t i = 1; i < n; i++) {
    bx0 = fminf(bx0, h[2 * i]);
    by0 = fminf(by0, h[2 * i + 1]);
    bx1 = fmaxf(bx1, h[2 * i]);
    by1 = fmaxf(by1, h[2 * i + 1]);
  }
  *x0 = (int32_t)floorf(bx0 / g->ls);
  *y0 = (int32_t)floorf(by0 / g->ls);
  *x1 = (int32_t)floorf(bx1 / g->ls);
  *y1 = (int32_t)floorf(by1 / g->ls);
  if(*x0 < 0) *x0 = 0;
  if(*y0 < 0) *y0 = 0;
  if(*x1 >= g->lw) *x1 = g->lw - 1;
  if(*y1 >= g->lh) *y1 = g->lh - 1;
}

int piecegeo_index(PieceGeo * g) {
  g->ls = g->cell;
  g->lw = (uint16_t)(g->width / g->ls) + 1;
  g->lh = (uint16_t)(g->height / g->ls) + 1;
  uint32_t squares = (uint32_t)g->lw * g->lh;
  g->lstart = calloc(squares + 1, sizeof(uint32_t));
  if(!g->lstart) return 1;
  // Count, prefix sum, then fill (lstart[i + 1] is the fill cursor for square i)
  for(uint32_t p = 0; p < g->count; p++) {
    int32_t x0, y0, x1, y1;
    piecegeo_span(g, p, &x0, &y0, &x1, &y1);
    for(int32_t y = y0; y <= y1; y++) for(int32_t x = x0; x <= x1; x++) g->lstart[y * g->lw + x + 1]++;
  }
  for(uint32_t i = 0; i < squares; i++) g->lstart[i + 1] += g->lstart[i];
  g->lpiece = malloc(sizeof(uint16_t) * (g->lstart[squares] ? g->lstart[squares] : 1));
  if(!g->lpiece) return 1;
  uint32_t * fill = malloc(sizeof(uint32_t) * squares);
  if(!fill) return 1;
  memcpy(fill, g->lstart, sizeof(uint32_t) * squares);
  for(uint32_t p = 0; p < g->count; p++) {
    int32_t x0, y0, x1, y1;
    piecegeo_span(g, p, &x0, &y0, &x1, &y1);
    for(int32_t y = y0; y <= y1; y++) for(int32_t x = x0; x <= x1; x++) g->lpiece[fill[y * g->lw + x]++] = p;
  }
  free(fill);
  return 0;
}

int piecegeo_grid(PieceGeo * g, uint16_t cols, uint16_t rows, float cell, const float * corners) {
  memset(g, 0, sizeof(PieceGeo));
  uint32_t count = (uint32_t)cols * rows;
  if(count == 0 || count > 0xFFF0 || !(cell > 0)) return 1;
  g->count = count;
  g->cols = cols;
  g->rows = rows;
  g->cell = cell;
  g->width = cols * cell;
  g->height = rows * cell;
  g->center = malloc(sizeof(float) * 2 * count);
  g->nbstart = malloc(sizeof(uint32_t) * (count + 1));
  g->nb = malloc(sizeof(uint16_t) * 4 * count);
  g->hbstart = malloc(sizeof(uint32_t) * (count + 1));
  g->hb = malloc(sizeof(float) * 8 * count);
  if(!g->center || !g->nbstart || !g->nb || !g->hbstart || !g->hb) {
    piecegeo_free(g);
    return 1;
  }
  uint32_t n = 0;
  for(uint32_t p = 0; p < count; p++) {
    uint32_t col = p % cols, row = p / cols;
    g->center[2 * p] = (col + 0.5f) * cell;
    g->center[2 * p + 1] = (row + 0.5f) * cell;
    g->nbstart[p] = n;
    if(row > 0) g->nb[n++] = p - cols;
    if(row + 1 < rows) g->nb[n++] = p + cols;
    if(col > 0) g->nb[n++] = p - 1;
    if(col + 1 < cols) g->nb[n++] = p + 1;
    g->hbstart[p] = 4 * p;
    // Clockwise from top left: (col, row), (col+1, row), (col+1, row+1), (col, row+1)
    float * h = g->hb + 8 * p;
    for(int k = 0; k < 4; k++) {
      uint32_t x = col + (k == 1 || k == 2), y = row + (k >= 2);
      const float * off = corners ? corners + 2 * (y * (cols + 1) + x) : NULL;
      h[2 * k] = (x + (off ? off[0] : 0)) * cell;
      h[2 * k + 1] = (y + (off ? off[1] : 0)) * cell;
    }
  }
  g->nbstart[count] = n;
  g->hbstart[count] = 4 * count;
  if(piecegeo_index(g)) {
    piecegeo_free(g);
    return 1;
  }
  return 0;
}

int32_t piecegeo_at(const PieceGeo * g, float x, float y) {
  if(!(x >= 0) || !(y >= 0)) return -1;
  uint32_t sx = (uint32_t)(x / g->ls), sy = (uint32_t)(y / g->ls);
  if(sx >= g->lw || sy >= g->lh) return -1;
  uint32_t s = sy * g->lw + sx;
  for(uint32_t i = g->lstart[s]; i < g->lstart[s + 1]; i++) {
    uint16_t p = g->lpiece[i];
    if(geom_inside(g->hb + 2 * g->hbstart[p], g->hbstart[p + 1] - g->hbstart[p], x, y)) return p;
  }
  return -1;
}

size_t piecegeo_size(const PieceGeo * g) {
  uint32_t nnb = g->nbstart[g->count], nhb = g->hbstart[g->count];
  return PIECEGEO_HEADER + 8 * (size_t)g->count + 4 * (size_t)(g->count + 1) + 2 * (size_t)nnb +
      4 * (size_t)(g->count + 1) + 8 * (size_t)nhb;
}

size_t piecegeo_write(const PieceGeo * g, uint8_t * buf) {
  uint8_t * p = buf;
  uint32_t nnb = g->nbstart[g->count], nhb = g->hbstart[g->count];
  memcpy(p, "PGEO", 4);
  p += 4;
  put16(&p, PIECEGEO_VERSION);
  put16(&p, g->cols);
  put16(&p, g->rows);
  putf(&p, g->cell);
  putf(&p, g->width);
  putf(&p, g->height);
  put32(&p, g->count);
  put32(&p, nnb);
  put32(&p, nhb);
  for(uint32_t i = 0; i < 2 * g->count; i++) putf(&p, g->center[i]);
  for(uint32_t i = 0; i <= g->count; i++) put32(&p, g->nbstart[i]);
  for(uint32_t i = 0; i < nnb; i++) put16(&p, g->nb[i]);
  for(uint32_t i = 0; i <= g->count; i++) put32(&p, g->hbstart[i]);
  for(uint32_t i = 0; i < 2 * nhb; i++) putf(&p, g->hb[i]);
  return p - buf;
}

// Reads a (count + 1) start array: must begin at 0, never decrease, end at total, and give
// every piece at least min entries. Returns 0 if good.
static int piecegeo_readstarts(const uint8_t ** p, uint32_t * out, uint32_t count, uint32_t total, uint32_t min) {
  for(uint32_t i = 0; i <= count; i++) out[i] = get32(p);
  if(out[0] != 0 || out[count] != total) return 1;
  for(uint32_t i = 0; i < count; i++) {
    if(out[i + 1] < out[i] || out[i + 1] - out[i] < min) return 1;
  }
  return 0;
}

int piecegeo_read(PieceGeo * g, const uint8_t * buf, size_t len) {
  memset(g, 0, sizeof(PieceGeo));
  if(len < PIECEGEO_HEADER || memcmp(buf, "PGEO", 4)) return 1;
  const uint8_t * p = buf + 4;
  if(get16(&p) != PIECEGEO_VERSION) return 2;
  g->cols = get16(&p);
  g->rows = get16(&p);
  g->cell = getf(&p);
  g->width = getf(&p);
  g->height = getf(&p);
  uint32_t count = get32(&p), nnb = get32(&p), nhb = get32(&p);
  if(count == 0 || count > 0xFFF0 || nnb > 64 * count || nhb > 1024 * count) return 3;
  if(!(g->cell > 0) || !isfinite(g->cell) || !(g->width > 0) || !(g->height > 0) ||
     g->width / g->cell > PIECEGEO_MAXSQUARES - 1 || g->height / g->cell > PIECEGEO_MAXSQUARES - 1) return 4;
  size_t need = PIECEGEO_HEADER + 8 * (size_t)count + 8 * (size_t)(count + 1) + 2 * (size_t)nnb + 8 * (size_t)nhb;
  if(len < need) return 5;
  g->count = count;
  g->center = malloc(sizeof(float) * 2 * count);
  g->nbstart = malloc(sizeof(uint32_t) * (count + 1));
  g->nb = malloc(sizeof(uint16_t) * (nnb ? nnb : 1));
  g->hbstart = malloc(sizeof(uint32_t) * (count + 1));
  g->hb = malloc(sizeof(float) * 2 * nhb);
  int err = 0;
  if(!g->center || !g->nbstart || !g->nb || !g->hbstart || !g->hb) err = 6;
  for(uint32_t i = 0; !err && i < 2 * count; i++) {
    g->center[i] = getf(&p);
    if(!isfinite(g->center[i])) err = 7;
  }
  if(!err && piecegeo_readstarts(&p, g->nbstart, count, nnb, 0)) err = 8;
  for(uint32_t i = 0; !err && i < nnb; i++) {
    g->nb[i] = get16(&p);
    if(g->nb[i] >= count) err = 9;
  }
  if(!err && piecegeo_readstarts(&p, g->hbstart, count, nhb, 3)) err = 10;
  for(uint32_t i = 0; !err && i < 2 * nhb; i++) {
    g->hb[i] = getf(&p);
    if(!isfinite(g->hb[i])) err = 11;
  }
  if(!err && piecegeo_index(g)) err = 6;
  if(err) piecegeo_free(g);
  return err;
}

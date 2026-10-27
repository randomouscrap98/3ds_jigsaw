#include "piecegeo.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

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

// Build the lookup grid from the hitboxes (square size = cell). Returns 0 on success.
static int piecegeo_index(PieceGeo * g) {
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

int piecegeo_grid(PieceGeo * g, uint16_t cols, uint16_t rows, float cell) {
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
    float x0 = col * cell, y0 = row * cell, x1 = x0 + cell, y1 = y0 + cell;
    g->center[2 * p] = x0 + cell / 2;
    g->center[2 * p + 1] = y0 + cell / 2;
    g->nbstart[p] = n;
    if(row > 0) g->nb[n++] = p - cols;
    if(row + 1 < rows) g->nb[n++] = p + cols;
    if(col > 0) g->nb[n++] = p - 1;
    if(col + 1 < cols) g->nb[n++] = p + 1;
    g->hbstart[p] = 4 * p;
    float * h = g->hb + 8 * p;
    h[0] = x0; h[1] = y0; h[2] = x1; h[3] = y0;
    h[4] = x1; h[5] = y1; h[6] = x0; h[7] = y1;
  }
  g->nbstart[count] = n;
  g->hbstart[count] = 4 * count;
  if(piecegeo_index(g)) {
    piecegeo_free(g);
    return 1;
  }
  return 0;
}

// Even-odd point in polygon. The crossing rule makes it half-open: points on a left or
// top edge are inside, on a right or bottom edge outside.
static int piecegeo_inside(const float * h, uint32_t n, float x, float y) {
  int in = 0;
  for(uint32_t i = 0, j = n - 1; i < n; j = i++) {
    float xi = h[2 * i], yi = h[2 * i + 1], xj = h[2 * j], yj = h[2 * j + 1];
    if((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
  }
  return in;
}

int32_t piecegeo_at(const PieceGeo * g, float x, float y) {
  if(!(x >= 0) || !(y >= 0)) return -1;
  uint32_t sx = (uint32_t)(x / g->ls), sy = (uint32_t)(y / g->ls);
  if(sx >= g->lw || sy >= g->lh) return -1;
  uint32_t s = sy * g->lw + sx;
  for(uint32_t i = g->lstart[s]; i < g->lstart[s + 1]; i++) {
    uint16_t p = g->lpiece[i];
    if(piecegeo_inside(g->hb + 2 * g->hbstart[p], g->hbstart[p + 1] - g->hbstart[p], x, y)) return p;
  }
  return -1;
}

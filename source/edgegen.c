#include "edgegen.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>

// Tab layout based on the well known bezier jigsaw shape (as in draradech's generator)
#define EDGEGEN_TABSIZE 0.1f
#define EDGEGEN_TABJITTER 0.012f
#define EDGEGEN_JITTER 0.04f

static void edgegen_random(Rng * r, EdgeTab * t) {
  t->size = EDGEGEN_TABSIZE + rng_range(r, -EDGEGEN_TABJITTER, EDGEGEN_TABJITTER);
  t->a = rng_range(r, -EDGEGEN_JITTER, EDGEGEN_JITTER);
  t->b = rng_range(r, -EDGEGEN_JITTER, EDGEGEN_JITTER);
  t->c = rng_range(r, -EDGEGEN_JITTER, EDGEGEN_JITTER);
  t->d = rng_range(r, -EDGEGEN_JITTER, EDGEGEN_JITTER);
  t->e = rng_range(r, -EDGEGEN_JITTER, EDGEGEN_JITTER);
  t->flip = (rng_next(r) & 0x100) ? 1 : -1;
}

int edgegen_init(EdgeGen * eg, uint16_t cols, uint16_t rows, uint32_t seed) {
  eg->cols = cols;
  eg->rows = rows;
  size_t nh = rows > 1 ? (size_t)(rows - 1) * cols : 0;
  size_t nv = cols > 1 ? (size_t)rows * (cols - 1) : 0;
  eg->h = malloc(sizeof(EdgeTab) * (nh ? nh : 1));
  eg->v = malloc(sizeof(EdgeTab) * (nv ? nv : 1));
  if(!eg->h || !eg->v) {
    edgegen_free(eg);
    return 1;
  }
  Rng r;
  rng_init(&r, seed);
  for(size_t i = 0; i < nh; i++) edgegen_random(&r, eg->h + i);
  for(size_t i = 0; i < nv; i++) edgegen_random(&r, eg->v + i);
  return 0;
}

void edgegen_free(EdgeGen * eg) {
  free(eg->h);
  free(eg->v);
  eg->h = NULL;
  eg->v = NULL;
}

// Sample tab in edge space: out gets 3 * SEGMENTS + 1 (t, n) pairs, from t=0 to t=1.
static int edgegen_sample(const EdgeTab * tab, float * out) {
  if(!tab) {
    out[0] = 0; out[1] = 0;
    out[2] = 1; out[3] = 0;
    return 2;
  }
  float t = tab->size, b = tab->b, c = tab->c, d = tab->d;
  float p[10][2] = {
    { 0.0f, 0.0f },
    { 0.2f, tab->a },
    { 0.5f + b + d, -t + c },
    { 0.5f - t + b, t + c },
    { 0.5f - 2 * t + b - d, 3 * t + c },
    { 0.5f + 2 * t + b - d, 3 * t + c },
    { 0.5f + t + b, t + c },
    { 0.5f + b + d, -t + c },
    { 0.8f, tab->e },
    { 1.0f, 0.0f },
  };
  int n = 0;
  out[n * 2] = 0; out[n * 2 + 1] = 0; n++;
  for(int curve = 0; curve < 3; curve++) {
    float (*q)[2] = p + curve * 3;
    for(int s = 1; s <= EDGEGEN_SEGMENTS; s++) {
      float u = (float)s / EDGEGEN_SEGMENTS, iu = 1 - u;
      float w0 = iu * iu * iu, w1 = 3 * iu * iu * u, w2 = 3 * iu * u * u, w3 = u * u * u;
      out[n * 2] = w0 * q[0][0] + w1 * q[1][0] + w2 * q[2][0] + w3 * q[3][0];
      out[n * 2 + 1] = (w0 * q[0][1] + w1 * q[1][1] + w2 * q[2][1] + w3 * q[3][1]) * tab->flip;
      n++;
    }
  }
  return n;
}

// Append one side of the piece. Edge space (t, n) is mapped to piece space by
// origin + t * along + n * across. All points except the last are appended (the
// next side starts there). reverse walks t from 1 to 0.
static int edgegen_side(const EdgeTab * tab, int reverse, float cell, float sx, float sy,
    float ax, float ay, float nx, float ny, float * xy) {
  float pts[(3 * EDGEGEN_SEGMENTS + 1) * 2];
  int n = edgegen_sample(tab, pts);
  for(int i = 0; i < n - 1; i++) {
    int k = reverse ? n - 1 - i : i;
    float t = pts[k * 2], u = pts[k * 2 + 1];
    xy[i * 2] = sx + (t * ax + u * nx) * cell;
    xy[i * 2 + 1] = sy + (t * ay + u * ny) * cell;
  }
  return n - 1;
}

int edgegen_outline(const EdgeGen * eg, uint16_t col, uint16_t row, float cell,
    float ox, float oy, float * xy) {
  uint16_t cols = eg->cols, rows = eg->rows;
  const EdgeTab * top = row > 0 ? eg->h + (row - 1) * cols + col : NULL;
  const EdgeTab * bottom = row + 1 < rows ? eg->h + row * cols + col : NULL;
  const EdgeTab * left = col > 0 ? eg->v + row * (cols - 1) + col - 1 : NULL;
  const EdgeTab * right = col + 1 < cols ? eg->v + row * (cols - 1) + col : NULL;
  int n = 0;
  // Horizontal edges: t runs +x, n runs +y. Vertical edges: t runs +y, n runs +x.
  // Both use the edge's own origin (left end / top end) so neighbours match exactly.
  n += edgegen_side(top, 0, cell, ox, oy, 1, 0, 0, 1, xy + n * 2);
  n += edgegen_side(right, 0, cell, ox + cell, oy, 0, 1, 1, 0, xy + n * 2);
  n += edgegen_side(bottom, 1, cell, ox, oy + cell, 1, 0, 0, 1, xy + n * 2);
  n += edgegen_side(left, 1, cell, ox, oy, 0, 1, 1, 0, xy + n * 2);
  return n;
}

#include "edgegen.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

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

int edgegen_init(EdgeGen * eg, uint16_t cols, uint16_t rows, uint32_t seed, float cornerjitter) {
  eg->cols = cols;
  eg->rows = rows;
  size_t nh = rows > 1 ? (size_t)(rows - 1) * cols : 0;
  size_t nv = cols > 1 ? (size_t)rows * (cols - 1) : 0;
  size_t nc = (size_t)(cols + 1) * (rows + 1);
  eg->h = malloc(sizeof(EdgeTab) * (nh ? nh : 1));
  eg->v = malloc(sizeof(EdgeTab) * (nv ? nv : 1));
  eg->corners = malloc(sizeof(float) * 2 * nc);
  if(!eg->h || !eg->v || !eg->corners) {
    edgegen_free(eg);
    return 1;
  }
  Rng r;
  rng_init(&r, seed);
  for(size_t i = 0; i < nh; i++) edgegen_random(&r, eg->h + i);
  for(size_t i = 0; i < nv; i++) edgegen_random(&r, eg->v + i);
  // Corners come last so a classic cut (no jitter) is identical to before they existed
  for(uint16_t y = 0; y <= rows; y++) {
    for(uint16_t x = 0; x <= cols; x++) {
      float * c = eg->corners + 2 * ((size_t)y * (cols + 1) + x);
      c[0] = c[1] = 0;
      if(cornerjitter <= 0) continue;
      if(x > 0 && x < cols) c[0] = rng_range(&r, -cornerjitter, cornerjitter);
      if(y > 0 && y < rows) c[1] = rng_range(&r, -cornerjitter, cornerjitter);
    }
  }
  return 0;
}

void edgegen_free(EdgeGen * eg) {
  free(eg->h);
  free(eg->v);
  free(eg->corners);
  eg->h = NULL;
  eg->v = NULL;
  eg->corners = NULL;
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

// Append one side of the piece, along the edge line from a to b. Edge space (t, n) maps
// to a + t * (b - a) + n * cell * N, where N is the unit normal pointing toward +y for
// horizontal edges and +x for vertical ones (so both neighbours agree on it). All points
// except the last are appended (the next side starts there). reverse walks b to a.
static int edgegen_side(const EdgeTab * tab, int reverse, int vertical, float cell,
    float ax, float ay, float bx, float by, float * xy) {
  float pts[(3 * EDGEGEN_SEGMENTS + 1) * 2];
  int n = edgegen_sample(tab, pts);
  float ux = bx - ax, uy = by - ay;
  float len = sqrtf(ux * ux + uy * uy);
  float nx = -uy / len, ny = ux / len;
  if((vertical && nx < 0) || (!vertical && ny < 0)) { nx = -nx; ny = -ny; }
  for(int i = 0; i < n - 1; i++) {
    int k = reverse ? n - 1 - i : i;
    float t = pts[k * 2], u = pts[k * 2 + 1] * cell;
    xy[i * 2] = ax + t * ux + u * nx;
    xy[i * 2 + 1] = ay + t * uy + u * ny;
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
  // The four (possibly jittered) corners of this cell, in output coordinates
  float c[4][2];
  for(int i = 0; i < 4; i++) {
    uint16_t x = col + (i & 1), y = row + (i >> 1);
    const float * off = eg->corners + 2 * ((size_t)y * (cols + 1) + x);
    c[i][0] = ox + ((i & 1) + off[0]) * cell;
    c[i][1] = oy + ((i >> 1) + off[1]) * cell;
  }
  // 0 = top left, 1 = top right, 2 = bottom left, 3 = bottom right. Every edge is
  // generated in its canonical direction (left to right, top to bottom) and reversed
  // as needed for a clockwise walk, so neighbours produce identical points.
  int n = 0;
  n += edgegen_side(top, 0, 0, cell, c[0][0], c[0][1], c[1][0], c[1][1], xy + n * 2);
  n += edgegen_side(right, 0, 1, cell, c[1][0], c[1][1], c[3][0], c[3][1], xy + n * 2);
  n += edgegen_side(bottom, 1, 0, cell, c[2][0], c[2][1], c[3][0], c[3][1], xy + n * 2);
  n += edgegen_side(left, 1, 1, cell, c[0][0], c[0][1], c[2][0], c[2][1], xy + n * 2);
  return n;
}

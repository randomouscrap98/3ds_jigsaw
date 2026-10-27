#include "edgegen.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

// Tab layout based on the well known bezier jigsaw shape (as in draradech's generator)
#define EDGEGEN_TABSIZE 0.1f

static const EdgeStyle edgegen_styles[EDGEGEN_STYLES] = {
  { .corner = 0.0f, .tabsize = 0.012f, .shape = 0.04f, .reach = 0.35f },  // Classic
  { .corner = 0.1f, .tabsize = 0.012f, .shape = 0.04f, .reach = 0.45f },  // Irregular
  { .corner = 0.2f, .tabsize = 0.025f, .shape = 0.06f, .reach = 0.55f },  // Wild
};

const EdgeStyle * edgegen_style(uint8_t style) {
  return edgegen_styles + (style < EDGEGEN_STYLES ? style : EDGEGEN_STYLE_CLASSIC);
}

void edgegen_randomtab(Rng * r, const EdgeStyle * st, EdgeTab * t) {
  t->size = EDGEGEN_TABSIZE + rng_range(r, -st->tabsize, st->tabsize);
  t->a = rng_range(r, -st->shape, st->shape);
  t->b = rng_range(r, -st->shape, st->shape);
  t->c = rng_range(r, -st->shape, st->shape);
  t->d = rng_range(r, -st->shape, st->shape);
  t->e = rng_range(r, -st->shape, st->shape);
  t->flip = (rng_next(r) & 0x100) ? 1 : -1;
}

// Jitter the inner corners of cell (col, row); border corners only slide along the border
static void edgegen_corners(EdgeGen * eg, Rng * r, uint16_t col, uint16_t row) {
  for(int k = 0; k < 4; k++) {
    uint16_t x = col + (k & 1), y = row + (k >> 1);
    float * c = eg->corners + 2 * ((size_t)y * (eg->cols + 1) + x);
    if(x > 0 && x < eg->cols) c[0] = rng_range(r, -eg->style.corner, eg->style.corner);
    if(y > 0 && y < eg->rows) c[1] = rng_range(r, -eg->style.corner, eg->style.corner);
  }
}

// New random edges and corners for one piece (neighbours share them, so they change too)
static void edgegen_reroll(EdgeGen * eg, uint32_t seed, uint16_t col, uint16_t row, int pass) {
  uint16_t cols = eg->cols, rows = eg->rows;
  Rng r;
  rng_init(&r, seed ^ ((uint32_t)(row * cols + col) * 2654435761u) ^ ((uint32_t)(pass + 1) * 0x9E3779B9u));
  if(row > 0) edgegen_randomtab(&r, &eg->style, eg->h + (row - 1) * cols + col);
  if(row + 1 < rows) edgegen_randomtab(&r, &eg->style, eg->h + row * cols + col);
  if(col > 0) edgegen_randomtab(&r, &eg->style, eg->v + row * (cols - 1) + col - 1);
  if(col + 1 < cols) edgegen_randomtab(&r, &eg->style, eg->v + row * (cols - 1) + col);
  if(eg->style.corner > 0) edgegen_corners(eg, &r, col, row);
}

// Check every piece; reroll the bad ones and recheck around them until all pass.
// Returns 0 if everything passed.
static int edgegen_validate(EdgeGen * eg, uint32_t seed) {
  uint16_t cols = eg->cols, rows = eg->rows;
  uint8_t * check = malloc((size_t)cols * rows);
  if(!check) return 1;
  memset(check, 1, (size_t)cols * rows);
  int result = 1;
  for(int pass = 0; pass < EDGEGEN_MAXPASSES; pass++) {
    // Pieces rerolled this pass; their neighbours (incl. diagonal, via corners) need a recheck
    int any = 0;
    for(uint16_t row = 0; row < rows; row++) {
      for(uint16_t col = 0; col < cols; col++) {
        uint8_t * c = check + row * cols + col;
        if(!(*c & 1)) continue;
        *c &= ~1;
        if(edgegen_piece_ok(eg, col, row)) continue;
        edgegen_reroll(eg, seed, col, row, pass);
        any = 1;
        for(int dy = -1; dy <= 1; dy++) {
          for(int dx = -1; dx <= 1; dx++) {
            int x = col + dx, y = row + dy;
            if(x >= 0 && y >= 0 && x < cols && y < rows) check[y * cols + x] |= 2;
          }
        }
      }
    }
    if(!any) {
      result = 0;
      break;
    }
    for(size_t i = 0; i < (size_t)cols * rows; i++) check[i] = check[i] >> 1;
  }
  free(check);
  return result;
}

int edgegen_init(EdgeGen * eg, uint16_t cols, uint16_t rows, uint32_t seed, const EdgeStyle * style) {
  eg->cols = cols;
  eg->rows = rows;
  eg->style = *style;
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
  for(size_t i = 0; i < nh; i++) edgegen_randomtab(&r, style, eg->h + i);
  for(size_t i = 0; i < nv; i++) edgegen_randomtab(&r, style, eg->v + i);
  // Corners come last so a classic cut (no jitter) is identical to before they existed
  memset(eg->corners, 0, sizeof(float) * 2 * nc);
  if(style->corner > 0) {
    for(uint16_t y = 0; y <= rows; y++) {
      for(uint16_t x = 0; x <= cols; x++) {
        float * c = eg->corners + 2 * ((size_t)y * (cols + 1) + x);
        if(x > 0 && x < cols) c[0] = rng_range(&r, -style->corner, style->corner);
        if(y > 0 && y < rows) c[1] = rng_range(&r, -style->corner, style->corner);
      }
    }
  }
  // Not converging would only leave an odd looking piece, so it isn't an error
  edgegen_validate(eg, seed);
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

int edgegen_tabcurve(const EdgeTab * tab, float * out) {
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
  int n = edgegen_tabcurve(tab, pts);
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

static int edgegen_crosses(const float * a, const float * b, const float * c, const float * d) {
  float d1 = (d[0] - c[0]) * (a[1] - c[1]) - (d[1] - c[1]) * (a[0] - c[0]);
  float d2 = (d[0] - c[0]) * (b[1] - c[1]) - (d[1] - c[1]) * (b[0] - c[0]);
  float d3 = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
  float d4 = (b[0] - a[0]) * (d[1] - a[1]) - (b[1] - a[1]) * (d[0] - a[0]);
  return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

// Squared distance from point p to segment ab
static float edgegen_dist2(const float * p, const float * a, const float * b) {
  float vx = b[0] - a[0], vy = b[1] - a[1], wx = p[0] - a[0], wy = p[1] - a[1];
  float l = vx * vx + vy * vy;
  float t = l > 0 ? (vx * wx + vy * wy) / l : 0;
  if(t < 0) t = 0;
  if(t > 1) t = 1;
  float dx = wx - t * vx, dy = wy - t * vy;
  return dx * dx + dy * dy;
}

int edgegen_piece_ok(const EdgeGen * eg, uint16_t col, uint16_t row) {
  float xy[EDGEGEN_MAXPOINTS * 2];
  int n = edgegen_outline(eg, col, row, 1.0f, 0, 0, xy);
  const float lo = -eg->style.reach, hi = 1 + eg->style.reach;
  for(int i = 0; i < n; i++) {
    if(xy[i * 2] < lo || xy[i * 2] > hi || xy[i * 2 + 1] < lo || xy[i * 2 + 1] > hi) return 0;
  }
  return edgegen_shape_ok(xy, n, EDGEGEN_CLEARANCE);
}

int edgegen_shape_ok(const float * xy, int n, float cl) {
  for(int i = 0; i < n; i++) {
    const float * a = xy + i * 2, * b = xy + ((i + 1) % n) * 2;
    for(int j = i + 2; j < n; j++) {
      if(i == 0 && j == n - 1) continue;  // Shares a point with segment 0
      const float * c = xy + j * 2, * d = xy + ((j + 1) % n) * 2;
      // Far apart: can't cross or be too close
      if(fminf(a[0], b[0]) > fmaxf(c[0], d[0]) + cl || fminf(c[0], d[0]) > fmaxf(a[0], b[0]) + cl ||
         fminf(a[1], b[1]) > fmaxf(c[1], d[1]) + cl || fminf(c[1], d[1]) > fmaxf(a[1], b[1]) + cl) continue;
      if(edgegen_crosses(a, b, c, d)) return 0;
      int gap = j - i;
      if(gap > n / 2) gap = n - gap;
      if(gap > EDGEGEN_CLEARSKIP && edgegen_dist2(a, c, d) < cl * cl) return 0;
    }
  }
  return 1;
}

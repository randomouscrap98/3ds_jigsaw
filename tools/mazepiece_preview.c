// Maze piece preview: mazepiece_preview OUT.png PIECES AVGCELLS NEWEST SEED
// Draws the pieces as flat cells (wall pieces darker), prints shape stats.
#include "mazepiece.h"
#include "layout.h"
#include "rng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

typedef struct {
  uint8_t * key;
  uint32_t len;
} Shape;

static int shape_cmp(const void * a, const void * b) {
  const Shape * x = a, * y = b;
  if(x->len != y->len) return x->len < y->len ? -1 : 1;
  return memcmp(x->key, y->key, x->len);
}

// Canonical key: smallest of the 4 rotations, as (w, h, row bytes)
static void shape_key(const uint8_t * bm, int w, int h, Shape * out) {
  int maxd = w > h ? w : h;
  uint8_t * best = NULL, * cur = malloc(2 + maxd * maxd);
  uint32_t len = 2 + (uint32_t)w * h;
  for(int rot = 0; rot < 4; rot++) {
    int rw = rot & 1 ? h : w, rh = rot & 1 ? w : h;
    cur[0] = rw; cur[1] = rh;
    for(int y = 0; y < rh; y++) for(int x = 0; x < rw; x++) {
      int sx, sy;
      if(rot == 0) { sx = x; sy = y; }
      else if(rot == 1) { sx = y; sy = h - 1 - x; }
      else if(rot == 2) { sx = w - 1 - x; sy = h - 1 - y; }
      else { sx = w - 1 - y; sy = x; }
      cur[2 + y * rw + x] = bm[sy * w + sx];
    }
    if(!best) { best = malloc(len); memcpy(best, cur, len); }
    else if(memcmp(cur, best, len) < 0) memcpy(best, cur, len);
  }
  free(cur);
  out->key = best;
  out->len = len;
}

static void hsl(float hh, float s, float l, uint8_t * px) {
  float c = (1 - fabsf(2 * l - 1)) * s, hp = hh * 6, x = c * (1 - fabsf(fmodf(hp, 2) - 1));
  float r = 0, g = 0, b = 0;
  int i = (int)hp;
  if(i == 0) { r = c; g = x; } else if(i == 1) { r = x; g = c; } else if(i == 2) { g = c; b = x; }
  else if(i == 3) { g = x; b = c; } else if(i == 4) { r = x; b = c; } else { r = c; b = x; }
  float mm = l - c / 2;
  px[0] = (r + mm) * 255; px[1] = (g + mm) * 255; px[2] = (b + mm) * 255;
}

int main(int argc, char ** argv) {
  if(argc < 6) {
    fprintf(stderr, "usage: %s OUT.png PIECES AVGCELLS NEWEST SEED [CAP]\n", argv[0]);
    return 1;
  }
  uint32_t target = atoi(argv[2]), avg = atoi(argv[3]), seed = atoi(argv[5]);
  float newest = atof(argv[4]), cap = argc > 6 ? atof(argv[6]) : 0;
  uint16_t cols, rows;
  layout_grid(1024, 768, target, &cols, &rows);
  MazePieces m;
  if(mazepiece_init(&m, cols, rows, cols * rows, avg, newest, cap, seed)) { fprintf(stderr, "failed\n"); return 1; }
  int W = m.w, H = m.h;
  uint32_t n = m.count;
  // Per piece: cells, bbox, ends / bends / branches (by same-piece neighbours)
  uint32_t * cells = calloc(n, sizeof(uint32_t)), * bends = calloc(n, sizeof(uint32_t)), * branch = calloc(n, sizeof(uint32_t));
  int * x0 = malloc(sizeof(int) * n), * y0 = malloc(sizeof(int) * n), * x1 = malloc(sizeof(int) * n), * y1 = malloc(sizeof(int) * n);
  for(uint32_t p = 0; p < n; p++) { x0[p] = y0[p] = 1 << 30; x1[p] = y1[p] = -1; }
  uint32_t unlabeled = 0;
  for(int y = 0; y < H; y++) for(int x = 0; x < W; x++) {
    int32_t l = m.label[y * W + x];
    if(l < 0) { unlabeled++; continue; }
    cells[l]++;
    if(x < x0[l]) x0[l] = x;
    if(y < y0[l]) y0[l] = y;
    if(x > x1[l]) x1[l] = x;
    if(y > y1[l]) y1[l] = y;
    int nb[4], deg = 0;
    const int dx[4] = { 1, 0, -1, 0 }, dy[4] = { 0, 1, 0, -1 };
    for(int d = 0; d < 4; d++) {
      int nx = x + dx[d], ny = y + dy[d];
      nb[d] = nx >= 0 && ny >= 0 && nx < W && ny < H && m.label[ny * W + nx] == l;
      deg += nb[d];
    }
    if(deg >= 3) branch[l]++;
    if(deg == 2 && !((nb[0] && nb[2]) || (nb[1] && nb[3]))) bends[l]++;
  }
  float aspsum = 0, areasum = 0, sidemax = 0, longsum = 0;
  uint32_t longp = 0;
  uint32_t cmin = UINT32_MAX, cmax = 0, simple = 0, sumturn = 0, turnmin = UINT32_MAX, boxmin = UINT32_MAX;
  Shape * shapes = malloc(sizeof(Shape) * n);
  for(uint32_t p = 0; p < n; p++) {
    if(cells[p] < cmin) cmin = cells[p];
    if(cells[p] > cmax) cmax = cells[p];
    uint32_t turns = bends[p] + branch[p];
    sumturn += turns;
    if(turns < turnmin) turnmin = turns;
    simple += turns < 3;
    int bw = x1[p] - x0[p] + 1, bh = y1[p] - y0[p] + 1;
    float asp = bw > bh ? (float)bw / bh : (float)bh / bw;
    aspsum += asp;
    float cw = (float)cols / W;
    areasum += bw * bh * cw * cw;
    longsum += (bw > bh ? bw : bh) * cw;
    if((bw > bh ? bw : bh) * cw > sidemax) sidemax = (bw > bh ? bw : bh) * cw;
    longp += asp > 2;
    if((uint32_t)(bw < bh ? bw : bh) < boxmin) boxmin = bw < bh ? bw : bh;
    uint8_t * bm = calloc(bw * bh, 1);
    for(int y = y0[p]; y <= y1[p]; y++) for(int x = x0[p]; x <= x1[p]; x++) bm[(y - y0[p]) * bw + x - x0[p]] = m.label[y * W + x] == (int32_t)p;
    shape_key(bm, bw, bh, shapes + p);
    free(bm);
  }
  qsort(shapes, n, sizeof(Shape), shape_cmp);
  uint32_t dups = 0;
  for(uint32_t p = 1; p < n; p++) dups += !shape_cmp(shapes + p - 1, shapes + p);
  printf("%s: %ux%u pieces target %u -> %u, grid %dx%d, cell %.3f piece, thr %u, tiny %u, flips %u, capsplits %u, minmerges %u, unlabeled %u\n",
      argv[1], cols, rows, cols * rows, n, W, H, (float)cols / W, m.thr, m.tiny, m.flips, m.capsplits, m.minmerges, unlabeled);
  printf("  cells min %u avg %.1f max %u | bends+branches min %u avg %.1f, <3: %u | min bbox side %u cells | duplicate shapes %u\n  bbox aspect avg %.2f, > 2: %u | bbox area avg %.2f pieces^2, long side avg %.2f max %.2f pieces\n",
      cmin, (float)(W * H) / n, cmax, turnmin, (float)sumturn / n, simple, boxmin, dups, aspsum / n, longp, areasum / n, longsum / n, sidemax);

  // Picture: flat colors, wall pieces darker, dark line between pieces
  int s = 1600 / W < 4 ? 4 : 1600 / W;
  int IW = W * s, IH = H * s;
  uint8_t * img = malloc((size_t)IW * IH * 3);
  uint8_t * col = malloc(n * 3);
  Rng r;
  rng_init(&r, 99);
  for(uint32_t p = 0; p < n; p++) col[p * 3] = 0;
  for(int i = 0; i < W * H; i++) {
    int32_t l = m.label[i];
    if(l >= 0 && !col[l * 3]) {
      hsl(rng_float(&r), 0.55f, m.wall[i] ? 0.45f : 0.72f, col + l * 3);
      if(!col[l * 3]) col[l * 3] = 1;
    }
  }
  for(int y = 0; y < IH; y++) for(int x = 0; x < IW; x++) {
    int cx = x / s, cy = y / s;
    int32_t l = m.label[cy * W + cx];
    uint8_t * px = img + ((size_t)y * IW + x) * 3;
    if(l < 0) { px[0] = 255; px[1] = 0; px[2] = 255; continue; }
    memcpy(px, col + l * 3, 3);
    int edge = (x % s == 0 && cx > 0 && m.label[cy * W + cx - 1] != l) || (y % s == 0 && cy > 0 && m.label[(cy - 1) * W + cx] != l);
    if(edge) px[0] = px[1] = px[2] = 30;
  }
  stbi_write_png(argv[1], IW, IH, 3, img, IW * 3);
  return 0;
}

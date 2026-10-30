#include "snakecut.h"
#include "strokecut.h"
#include "pottscut.h"
#include "geom.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef SNAKECUT_DEBUG
#include <stdio.h>
#endif

static const SnakeStyle snakecut_styles[SNAKECUT_STYLES] = {
  { .res = 20, .geo = 1, .relax = 3, .pstraight = 0.45f, .psaw = 0.2f, .psquare = 0.15f, .pchevron = 0.15f,
    .depth = 0.12f, .period = 0.25f, .margin = 0.12f, .simplify = 0.75f,
    .base = 1.25f, .snakes = 0.5f, .pairs = 0.1f, .wmin = 0.25f, .wmax = 0.35f, .lmin = 1.2f,
    .lead0 = 0.3f, .lead1 = 0.8f, .emin = 0.3f, .emax = 0.7f, .ecc = 1.8f, .sweep0 = 3.5f, .sweep1 = 5.6f,
    .sbend = 0.3f, .s1sweep0 = 1.6f, .s1sweep1 = 2.6f, .twin = 0.35f, .mirror = 0.5f, .taper = 0.08f, .gap = 0.22f, .thin = 0.08f, .minpart = 0.35f, .tiny = 0.35f,
    .ywmin = 0.32f, .ywmax = 0.42f, .yturns0 = 1.0f, .yturns1 = 1.5f, .smooth = 12 },
};

const SnakeStyle * snakecut_style(uint8_t style) {
  return snakecut_styles + (style < SNAKECUT_STYLES ? style : SNAKECUT_STYLE_DEFAULT);
}

#define SK_PI 3.14159265f

typedef struct {
  int w, h;
  float res;
  const SnakeStyle * st;
  Rng r;
  int32_t * lab;      // Piece per square (-2: to be handed to a neighbour)
  int32_t * snk;      // Snake per square (0: none)
  int32_t * mark;     // Attempt that marked the square (own body while marching)
  float * marks;      // ... and how far along it was then (squares)
  int32_t * body;     // Squares marked by the current attempt
  int nbody;
  int32_t attempt, nsnake;
  int32_t * edge;     // Squares on a boundary between pieces (picked from when starting snakes)
  int nedge;
  uint8_t * sent;     // Snakes per piece
  uint32_t nlab;
} Sk;

static int sk_other4(const Sk * k, int i, int32_t * other) {
  int x = i % k->w, y = i / k->w;
  int nb[4] = { x > 0 ? i - 1 : -1, x < k->w - 1 ? i + 1 : -1, y > 0 ? i - k->w : -1, y < k->h - 1 ? i + k->w : -1 };
  for(int j = 0; j < 4; j++) {
    if(nb[j] >= 0 && k->lab[nb[j]] != k->lab[i]) {
      if(other) *other = k->lab[nb[j]];
      return 1;
    }
  }
  return 0;
}

// Unit direction out of piece a at square i (away from a's squares nearby); 0 if unclear
static int sk_outward(const Sk * k, int i, int32_t a, float rad, float * hx, float * hy) {
  int cx = i % k->w, cy = i / k->w, ri = (int)ceilf(rad);
  float gx = 0, gy = 0;
  int cnt = 0;
  for(int dy = -ri; dy <= ri; dy++) {
    for(int dx = -ri; dx <= ri; dx++) {
      int x = cx + dx, y = cy + dy;
      if(dx * dx + dy * dy > rad * rad || x < 0 || y < 0 || x >= k->w || y >= k->h) continue;
      cnt++;
      if(k->lab[y * k->w + x] == a) { gx += dx; gy += dy; }
    }
  }
  float l = hypotf(gx, gy);
  if(cnt == 0 || l < 0.15f * cnt) return 0;
  *hx = -gx / l;
  *hy = -gy / l;
  return 1;
}

// 1 if a disc (squares) at (x, y) is inside the grid and clear of other snakes, of this
// attempt's own body marked more than back squares ago, and of piece avoid (-1: none)
static int sk_clear(const Sk * k, float x, float y, float rad, float s, float back, int32_t avoid) {
  if(x - rad < 0 || y - rad < 0 || x + rad > k->w || y + rad > k->h) return 0;
  int x0 = (int)floorf(x - rad), x1 = (int)ceilf(x + rad), y0 = (int)floorf(y - rad), y1 = (int)ceilf(y + rad);
  for(int py = y0; py <= y1; py++) {
    for(int px = x0; px <= x1; px++) {
      if(px < 0 || py < 0 || px >= k->w || py >= k->h) continue;
      float dx = px + 0.5f - x, dy = py + 0.5f - y;
      if(dx * dx + dy * dy > rad * rad) continue;
      int i = py * k->w + px;
      if(k->snk[i]) return 0;
      if(k->lab[i] == avoid && k->mark[i] != k->attempt) return 0;
      if(back > 0 && k->mark[i] == k->attempt && s - k->marks[i] > back) return 0;
    }
  }
  return 1;
}

// Mark a disc as this attempt's body. Returns 0, or -1 out of memory.
static int sk_markdisc(Sk * k, float x, float y, float rad, float s) {
  int x0 = (int)floorf(x - rad), x1 = (int)ceilf(x + rad), y0 = (int)floorf(y - rad), y1 = (int)ceilf(y + rad);
  for(int py = y0; py <= y1; py++) {
    for(int px = x0; px <= x1; px++) {
      if(px < 0 || py < 0 || px >= k->w || py >= k->h) continue;
      float dx = px + 0.5f - x, dy = py + 0.5f - y;
      if(dx * dx + dy * dy > rad * rad) continue;
      int i = py * k->w + px;
      if(k->mark[i] == k->attempt) continue;
      k->mark[i] = k->attempt;
      k->marks[i] = s;
      k->body[k->nbody++] = i;
    }
  }
  return 0;
}

static void sk_commit(Sk * k, int32_t a, int32_t b, const uint8_t * arm) {
  k->nsnake++;
  for(int j = 0; j < k->nbody; j++) {
    int i = k->body[j];
    k->lab[i] = arm && arm[j] ? b : a;
    k->snk[i] = k->nsnake;
  }
}

// A snake's middle line, in its own frame (starts at the origin heading +x, squares)
typedef struct {
  float w, lead;
  int arcs;
  float a[2], b[2], t0[2], sweep[2], sign[2];
} SkShape;

#define SK_PATHMAX 4096

// An ellipse arc (semi-axes a, b, starting at parameter t0) continuing from (x, y, ang),
// turning sweep radians, sign = +1 one way, -1 the other, sampled every ds
static int sk_arc(float * p, int n, float * x, float * y, float * ang, float a, float b, float t0, float sweep,
    float sign, float ds) {
  float tx = sign * -a * sinf(t0), ty = sign * b * cosf(t0);
  float phi = *ang - atan2f(ty, tx), cp = cosf(phi), sp = sinf(phi);
  float qx = a * cosf(t0), qy = b * sinf(t0);
  float cx = *x - (cp * qx - sp * qy), cy = *y - (sp * qx + cp * qy);
  float t = t0, turned = 0, last = *ang;
  while(turned < sweep && n < SK_PATHMAX) {
    float sx = -a * sinf(t), sy = b * cosf(t), sp2 = hypotf(sx, sy);
    t += sign * ds / sp2;
    qx = a * cosf(t); qy = b * sinf(t);
    *x = cx + cp * qx - sp * qy;
    *y = cy + sp * qx + cp * qy;
    tx = sign * -a * sinf(t); ty = sign * b * cosf(t);
    float na = atan2f(sp * tx + cp * ty, cp * tx - sp * ty) , d = na - last;
    while(d > SK_PI) d -= 2 * SK_PI;
    while(d < -SK_PI) d += 2 * SK_PI;
    turned += fabsf(d);
    last = na;
    p[2 * n] = *x;
    p[2 * n + 1] = *y;
    n++;
  }
  *ang = last;
  return n;
}

static int sk_path(float * p, const SkShape * sh, float ds) {
  int n = 0;
  float x = 0, y = 0, ang = 0;
  for(float s = 0; s < sh->lead && n < SK_PATHMAX; s += ds) { x += ds; p[2 * n] = x; p[2 * n + 1] = 0; n++; }
  for(int i = 0; i < sh->arcs; i++)
    n = sk_arc(p, n, &x, &y, &ang, sh->a[i], sh->b[i], sh->t0[i], sh->sweep[i], sh->sign[i], ds);
  return n;
}

// One ellipse with the hook open: tightest bend b^2/a (a the longer) at least rmin
static void sk_ellipse(Sk * k, float rmin, float * a, float * b) {
  const SnakeStyle * st = k->st;
  float lo = fmaxf(st->emin * k->res, rmin), hi = fmaxf(st->emax * k->res, lo);
  float s = rng_range(&k->r, lo, hi), l = s * rng_range(&k->r, 1, st->ecc);
  if(s * s / l < rmin) l = s * s / rmin;
  if(rng_below(&k->r, 2)) { *a = l; *b = s; } else { *a = s; *b = l; }
}

// Paint shape sh (mirrored if flip) from piece a's square i heading (hx, hy). 1 if placed.
static int sk_snakefrom(Sk * k, int i, int32_t a, float hx, float hy, const SkShape * sh, int flip, const float * path, int np) {
  const SnakeStyle * st = k->st;
  float w = sh->w, gap = st->gap * k->res;
  float ox = i % k->w + 0.5f - hx * w * 0.5f, oy = i / k->w + 0.5f - hy * w * 0.5f, fl = flip ? -1 : 1;
  k->attempt++;
  k->nbody = 0;
  int outside = 0, j = 0, left = 0;
  const float ds = 0.5f;
  for(; j < np; j++) {
    float lx = path[2 * j], ly = path[2 * j + 1] * fl;
    float x = ox + hx * lx - hy * ly, y = oy + hy * lx + hx * ly, s = (j + 1) * ds;
    float rad = w / 2 * (1 - st->taper * j / np);
    // Once out of its piece, it keeps clear of it (curling back onto it would close a hole)
    if(!sk_clear(k, x, y, rad + gap, s, 2 * (w + gap), left ? a : -1)) break;
    sk_markdisc(k, x, y, rad, s);
    int out = k->lab[(int)y * k->w + (int)x] != a;
    outside += out;
    left |= out && j * ds > w;
  }
  if(j * ds < st->lmin * k->res || outside < j * 0.4f) return 0;
  sk_commit(k, a, a, NULL);
  k->sent[a]++;
  return 1;
}

// One snake from a random boundary square (and maybe its twin). Returns snakes placed.
static int sk_snake(Sk * k, float * path) {
  const SnakeStyle * st = k->st;
  int i = k->edge[rng_below(&k->r, k->nedge)];
  int32_t a = k->lab[i];
  if(a < 0 || k->snk[i] || k->sent[a] >= 2 || !sk_other4(k, i, NULL)) return 0;
  float res = k->res, gap = st->gap * res, hx, hy;
  SkShape sh;
  sh.w = rng_range(&k->r, st->wmin, st->wmax) * res;
  if(!sk_outward(k, i, a, fmaxf(3, sh.w), &hx, &hy)) return 0;
  sh.lead = rng_range(&k->r, st->lead0, st->lead1) * res;
  float rmin = (sh.w + gap) / 2, sign = rng_below(&k->r, 2) ? 1 : -1;
  sh.arcs = rng_range(&k->r, 0, 1) < st->sbend ? 2 : 1;
  for(int j = 0; j < sh.arcs; j++) {
    sk_ellipse(k, rmin, &sh.a[j], &sh.b[j]);
    sh.t0[j] = rng_range(&k->r, 0, 2 * SK_PI);
    sh.sign[j] = j ? -sh.sign[0] : sign;
    sh.sweep[j] = j + 1 < sh.arcs ? rng_range(&k->r, st->s1sweep0, st->s1sweep1) : rng_range(&k->r, st->sweep0, st->sweep1);
  }
  int np = sk_path(path, &sh, 0.5f);
  if(!sk_snakefrom(k, i, a, hx, hy, &sh, 0, path, np)) return 0;
  if(rng_range(&k->r, 0, 1) >= st->twin) return 1;
  // Twin: from where a straight line back through the piece leaves it
  float x = i % k->w + 0.5f, y = i / k->w + 0.5f;
  int last = -1;
  for(float s = 0; s < 3 * res; s += 0.5f) {
    int X = (int)(x - hx * s), Y = (int)(y - hy * s);
    if(X < 0 || Y < 0 || X >= k->w || Y >= k->h) return 1;
    int c = Y * k->w + X;
    if(k->lab[c] != a) break;
    last = c;
  }
  if(last < 0 || !sk_other4(k, last, NULL) || k->snk[last]) return 1;
  return 1 + sk_snakefrom(k, last, a, -hx, -hy, &sh, rng_range(&k->r, 0, 1) < st->mirror, path, np);
}

// A yin-yang: two interleaved spiral bands around a point on the boundary between a and b,
// a's band reaching the rim on a's side. Returns 1 if placed, 0 if not, -1 out of memory.
static int sk_pair(Sk * k) {
  const SnakeStyle * st = k->st;
  int i = k->edge[rng_below(&k->r, k->nedge)];
  int32_t a = k->lab[i], b;
  if(a < 0 || k->snk[i] || !sk_other4(k, i, &b) || b < 0 || k->sent[a] >= 2 || k->sent[b] >= 2) return 0;
  float res = k->res, bw = rng_range(&k->r, st->ywmin, st->ywmax) * res, hx, hy;
  float c = bw / SK_PI, Rd = 2 * bw * rng_range(&k->r, st->yturns0, st->yturns1), gap = st->gap * res;
  if(!sk_outward(k, i, a, fmaxf(3, bw), &hx, &hy)) return 0;
  float cx = i % k->w + 0.5f, cy = i / k->w + 0.5f;
  if(!sk_clear(k, cx, cy, Rd + gap, 0, 0, -1)) return 0;
  k->attempt++;
  k->nbody = 0;
  sk_markdisc(k, cx, cy, Rd, 0);
  uint8_t * arm = malloc(k->nbody ? k->nbody : 1);
  if(!arm) return -1;
  float hand = rng_below(&k->r, 2) ? 1 : -1, ta = atan2f(-hy, -hx), off = Rd / c - SK_PI / 2;
  for(int j = 0; j < k->nbody; j++) {
    float dx = k->body[j] % k->w + 0.5f - cx, dy = k->body[j] / k->w + 0.5f - cy;
    float r = hypotf(dx, dy), phi = hand * (atan2f(dy, dx) - ta) + off;
    float f = (r / c - phi) / (2 * SK_PI);
    f -= floorf(f);
    arm[j] = f >= 0.5f;
  }
  sk_commit(k, a, b, arm);
  free(arm);
  k->sent[a]++;
  k->sent[b]++;
  return 1;
}

// Squares marked -2 go to their neighbours (most common labelled 4-neighbour), repeatedly
static void sk_absorb(Sk * k) {
  int changed = 1;
  while(changed) {
    changed = 0;
    for(int i = 0; i < k->w * k->h; i++) {
      if(k->lab[i] != -2) continue;
      int x = i % k->w, y = i / k->w;
      int nb[4] = { x > 0 ? i - 1 : -1, x < k->w - 1 ? i + 1 : -1, y > 0 ? i - k->w : -1, y < k->h - 1 ? i + k->w : -1 };
      int32_t best = -1;
      int bestn = 0;
      for(int j = 0; j < 4; j++) {
        if(nb[j] < 0 || k->lab[nb[j]] < 0) continue;
        int n = 0;
        for(int m = 0; m < 4; m++) n += nb[m] >= 0 && k->lab[nb[m]] == k->lab[nb[j]];
        if(n > bestn) { bestn = n; best = k->lab[nb[j]]; }
      }
      if(best >= 0) { k->lab[i] = best; changed = 1; }
    }
  }
}

// Chamfer distance (3/4 per square) within each piece from seeds (d = 0 seeds, else big)
static void sk_chamfer(const Sk * k, int32_t * d) {
  const int dx[4] = { -1, 0, 1, -1 }, dy[4] = { 0, -1, -1, -1 }, cost[4] = { 3, 3, 4, 4 };
  for(int pass = 0; pass < 2; pass++) {
    for(int y = 0; y < k->h; y++) {
      for(int x = 0; x < k->w; x++) {
        int i = y * k->w + x;
        for(int j = 0; j < 4; j++) {
          int X = x + dx[j], Y = y + dy[j];
          if(X < 0 || Y < 0 || X >= k->w || Y >= k->h) continue;
          int n = Y * k->w + X;
          if(k->lab[n] == k->lab[i] && d[n] + cost[j] < d[i]) d[i] = d[n] + cost[j];
        }
      }
    }
    for(int y = k->h - 1; y >= 0; y--) {
      for(int x = k->w - 1; x >= 0; x--) {
        int i = y * k->w + x;
        for(int j = 0; j < 4; j++) {
          int X = x - dx[j], Y = y - dy[j];
          if(X < 0 || Y < 0 || X >= k->w || Y >= k->h) continue;
          int n = Y * k->w + X;
          if(k->lab[n] == k->lab[i] && d[n] + cost[j] < d[i]) d[i] = d[n] + cost[j];
        }
      }
    }
  }
}

// Morphological opening per piece: squares not within thin of a square at least thin from
// other pieces (and the border) go to neighbours. Snake squares stay. Returns 0, or -1.
static int sk_thin(Sk * k) {
  int n = k->w * k->h;
  int32_t * d = malloc(sizeof(int32_t) * n);
  if(!d) return -1;
  int r3 = (int)(k->st->thin * k->res * 3 + 0.5f);
  for(int i = 0; i < n; i++) {
    int x = i % k->w, y = i / k->w, edge = x == 0 || y == 0 || x == k->w - 1 || y == k->h - 1;
    for(int dy = -1; dy <= 1 && !edge; dy++)
      for(int dx = -1; dx <= 1 && !edge; dx++) edge = k->lab[(y + dy) * k->w + x + dx] != k->lab[i];
    d[i] = edge ? 3 : 1 << 28;
  }
  sk_chamfer(k, d);
  for(int i = 0; i < n; i++) d[i] = d[i] >= r3 ? 0 : 1 << 28;
  sk_chamfer(k, d);
  for(int i = 0; i < n; i++) if(d[i] > r3 && !k->snk[i]) k->lab[i] = -2;
  free(d);
  sk_absorb(k);
  return 0;
}

// Each piece keeps its largest 4-connected part; other parts big enough become pieces, the
// rest goes to neighbours. Labels end up 0 .. nlab - 1 (sent follows). Returns 0, or -1.
static int sk_parts(Sk * k, float a0) {
  int n = k->w * k->h;
  int32_t * comp = malloc(sizeof(int32_t) * n), * queue = malloc(sizeof(int32_t) * n);
  int32_t * csize = NULL, * clab = NULL, * best = calloc(k->nlab, sizeof(int32_t));
  int ncomp = 0, err = !comp || !queue || !best;
  for(int i = 0; i < n && !err; i++) comp[i] = -1;
  for(int i = 0; i < n && !err; i++) {
    if(comp[i] >= 0) continue;
    int32_t l = k->lab[i];
    int qh = 0, qt = 0;
    queue[qt++] = i;
    comp[i] = ncomp;
    while(qh < qt) {
      int c = queue[qh++], x = c % k->w, y = c / k->w;
      int nb[4] = { x > 0 ? c - 1 : -1, x < k->w - 1 ? c + 1 : -1, y > 0 ? c - k->w : -1, y < k->h - 1 ? c + k->w : -1 };
      for(int j = 0; j < 4; j++) if(nb[j] >= 0 && comp[nb[j]] < 0 && k->lab[nb[j]] == l) { comp[nb[j]] = ncomp; queue[qt++] = nb[j]; }
    }
    int32_t * cs = realloc(csize, sizeof(int32_t) * (ncomp + 1)), * cl = realloc(clab, sizeof(int32_t) * (ncomp + 1));
    if(cs) csize = cs;
    if(cl) clab = cl;
    if(!cs || !cl) { err = 1; break; }
    csize[ncomp] = qt;
    clab[ncomp] = l;
    if(!best[l] || qt > csize[best[l] - 1]) best[l] = ncomp + 1;
    ncomp++;
  }
  // New labels: kept parts (main parts first in label order, then the big others)
  int32_t * nl = err ? NULL : malloc(sizeof(int32_t) * (ncomp ? ncomp : 1));
  uint8_t * ns = err ? NULL : calloc(ncomp ? ncomp : 1, 1);
  err = err || !nl || !ns;
  uint32_t m = 0;
  for(int c = 0; c < ncomp && !err; c++) nl[c] = -2;
  for(uint32_t l = 0; l < k->nlab && !err; l++) if(best[l]) { nl[best[l] - 1] = m; ns[m] = k->sent[l]; m++; }
  for(int c = 0; c < ncomp && !err; c++) if(nl[c] == -2 && csize[c] >= k->st->minpart * a0) nl[c] = m++;
  if(!err) {
    for(int i = 0; i < n; i++) k->lab[i] = nl[comp[i]];
    free(k->sent);
    k->sent = ns;
    ns = NULL;
    k->nlab = m;
    sk_absorb(k);
  }
  free(comp); free(queue); free(csize); free(clab); free(best); free(nl); free(ns);
  return err ? -1 : 0;
}

static uint32_t sk_find(uint32_t * p, uint32_t x) {
  while(p[x] != x) x = p[x] = p[p[x]];
  return x;
}

static int sk_cmpu64(const void * a, const void * b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return x < y ? -1 : x > y;
}

// Merge the smallest pieces into neighbours until count are left and none is smaller than
// tiny squares (labels end up 0 .. nlab - 1). Returns 0, 1 if there are fewer than count, -1 out of memory.
static int sk_merge(Sk * k, uint32_t count, float tiny) {
  if(k->nlab < count) return 1;
  int n = k->w * k->h, ne = 0, err = 0;
  uint64_t * keys = malloc(sizeof(uint64_t) * 2 * n);
  uint32_t * par = malloc(sizeof(uint32_t) * k->nlab), * area = calloc(k->nlab, sizeof(uint32_t));
  float * acc = calloc(k->nlab, sizeof(float));
  if(!keys || !par || !area || !acc) err = -1;
  for(int i = 0; i < n && !err; i++) {
    int x = i % k->w, y = i / k->w;
    area[k->lab[i]]++;
    int nb[2] = { x < k->w - 1 ? i + 1 : -1, y < k->h - 1 ? i + k->w : -1 };
    for(int j = 0; j < 2; j++) {
      if(nb[j] < 0 || k->lab[nb[j]] == k->lab[i]) continue;
      uint32_t a = k->lab[i], b = k->lab[nb[j]];
      keys[ne++] = a < b ? ((uint64_t)a << 32 | b) : ((uint64_t)b << 32 | a);
    }
  }
  uint32_t ue = 0;
  uint32_t * ea = NULL, * eb = NULL, * el = NULL;
  if(!err) {
    qsort(keys, ne, sizeof(uint64_t), sk_cmpu64);
    ea = malloc(sizeof(uint32_t) * (ne + 1)); eb = malloc(sizeof(uint32_t) * (ne + 1)); el = malloc(sizeof(uint32_t) * (ne + 1));
    if(!ea || !eb || !el) err = -1;
    for(int i = 0; i < ne && !err; i++) {
      if(i && keys[i] == keys[i - 1]) { el[ue - 1]++; continue; }
      ea[ue] = keys[i] >> 32; eb[ue] = (uint32_t)keys[i]; el[ue] = 1; ue++;
    }
  }
  uint32_t left = k->nlab;
  for(uint32_t l = 0; l < k->nlab && !err; l++) par[l] = l;
  while(!err && left > 1) {
    int64_t u = -1;
    for(uint32_t l = 0; l < k->nlab; l++) if(par[l] == l && (u < 0 || area[l] < area[u])) u = l;
    if(left <= count && area[u] >= tiny) break;
    for(uint32_t e = 0; e < ue; e++) {
      uint32_t a = sk_find(par, ea[e]), b = sk_find(par, eb[e]);
      if(a == b) continue;
      if(a == u) acc[b] += el[e];
      else if(b == u) acc[a] += el[e];
    }
    int64_t v = -1;
    float vs = -1;
    for(uint32_t e = 0; e < ue; e++) {
      uint32_t a = sk_find(par, ea[e]), b = sk_find(par, eb[e]), o = a == u ? b : b == u ? a : u;
      if(o == u || acc[o] <= 0) continue;
      float sc = acc[o] / sqrtf((float)area[o]) * (k->sent[o] ? 0.5f : 1);
      if(sc > vs) { vs = sc; v = o; }
    }
    for(uint32_t e = 0; e < ue; e++) { acc[sk_find(par, ea[e])] = 0; acc[sk_find(par, eb[e])] = 0; }
    if(v < 0) { err = 1; break; }
    par[u] = v;
    area[v] += area[u];
    k->sent[v] += k->sent[u];
    left--;
  }
#ifdef SNAKECUT_DEBUG
  if(!err) {
    uint32_t mn = 0xFFFFFFFFu;
    for(uint32_t l = 0; l < k->nlab; l++) if(par[l] == l && area[l] < mn) mn = area[l];
    fprintf(stderr, "snakecut: merged to %u, smallest %.2f of a piece\n", left, mn / (tiny / k->st->tiny));
  }
#endif
  if(!err && left < count) err = 1;
  if(!err) {
    uint32_t m = 0;
    for(uint32_t l = 0; l < k->nlab; l++) if(par[l] == l) area[l] = m++;
    for(int i = 0; i < n; i++) k->lab[i] = area[sk_find(par, k->lab[i])];
    k->nlab = m;
  }
  free(keys); free(par); free(area); free(acc); free(ea); free(eb); free(el);
  return err;
}

// ---- Geometric base ----

// Nearest two sites to (x, y) through a bucket grid (bs squares per bucket)
typedef struct {
  const float * s;
  uint32_t n;
  int bw, bh;
  float bs;
  int32_t * head, * next;
} SkSites;

static int sk_sitesinit(SkSites * ss, const float * s, uint32_t n, int w, int h, float bs) {
  ss->s = s; ss->n = n; ss->bs = bs;
  ss->bw = (int)ceilf(w / bs); ss->bh = (int)ceilf(h / bs);
  ss->head = malloc(sizeof(int32_t) * ss->bw * ss->bh);
  ss->next = malloc(sizeof(int32_t) * n);
  if(!ss->head || !ss->next) return -1;
  for(int i = 0; i < ss->bw * ss->bh; i++) ss->head[i] = -1;
  for(uint32_t i = 0; i < n; i++) {
    int bx = (int)(s[2 * i] / bs), by = (int)(s[2 * i + 1] / bs);
    bx = bx < 0 ? 0 : bx >= ss->bw ? ss->bw - 1 : bx;
    by = by < 0 ? 0 : by >= ss->bh ? ss->bh - 1 : by;
    ss->next[i] = ss->head[by * ss->bw + bx];
    ss->head[by * ss->bw + bx] = i;
  }
  return 0;
}

static void sk_sitesfree(SkSites * ss) { free(ss->head); free(ss->next); }

static void sk_near2(const SkSites * ss, float x, float y, int32_t * i1, int32_t * i2) {
  int bx = (int)(x / ss->bs), by = (int)(y / ss->bs);
  float d1 = 1e30f, d2 = 1e30f;
  *i1 = *i2 = -1;
  for(int r = 2; r < 64; r++) {
    for(int yy = by - r; yy <= by + r; yy++) {
      for(int xx = bx - r; xx <= bx + r; xx++) {
        if(xx < 0 || yy < 0 || xx >= ss->bw || yy >= ss->bh) continue;
        if(r > 2 && xx > bx - r && xx < bx + r && yy > by - r && yy < by + r) continue;   // Ring only
        for(int32_t i = ss->head[yy * ss->bw + xx]; i >= 0; i = ss->next[i]) {
          float dx = ss->s[2 * i] - x, dy = ss->s[2 * i + 1] - y, d = dx * dx + dy * dy;
          if(d < d1) { d2 = d1; *i2 = *i1; d1 = d; *i1 = i; }
          else if(d < d2) { d2 = d; *i2 = i; }
        }
      }
    }
    // Everything nearer than the searched ring's inner edge has been seen
    float reach = (r - 1) * ss->bs;
    if(*i2 >= 0 && d2 <= reach * reach) break;
  }
}

typedef struct {
  uint64_t key;
  float tmin, tmax;
  uint8_t kind;   // 0 straight, 1 saw, 2 square, 3 chevron, 4 step
  float sgn;
} SkPair;

static int sk_cmppair(const void * a, const void * b) {
  uint64_t x = ((const SkPair *)a)->key, y = ((const SkPair *)b)->key;
  return x < y ? -1 : x > y;
}

// Position of (x, y) relative to the bisector of sites a < b: t along it, d across (+ toward a)
static void sk_bis(const float * s, uint32_t a, uint32_t b, float x, float y, float * t, float * d) {
  float ax = s[2 * a], ay = s[2 * a + 1], bx = s[2 * b], by = s[2 * b + 1];
  float nx = ax - bx, ny = ay - by, l = hypotf(nx, ny);
  nx /= l; ny /= l;
  float mx = (ax + bx) / 2, my = (ay + by) / 2;
  *d = (x - mx) * nx + (y - my) * ny;
  *t = -(x - mx) * ny + (y - my) * nx;
}

// Decoration offset (squares, toward a) at t along a pair's edge
static float sk_decor(const Sk * k, const SkPair * p, float t) {
  const SnakeStyle * st = k->st;
  float m = st->margin * k->res, t0 = p->tmin + m, t1 = p->tmax - m, A = st->depth * k->res;
  if(p->kind == 0 || t1 - t0 < 2 * st->period * k->res * 0.6f) return 0;
  float u = (t - t0) / (t1 - t0);
  if(p->kind == 4) {
    float o = 0.75f * A;
    if(t < p->tmin || t > p->tmax) return 0;
    if(t < t0) return -o * (t - p->tmin) / m * p->sgn;
    if(t > t1) return o * (p->tmax - t) / m * p->sgn;
    return (u < 0.5f ? -o : o) * p->sgn;
  }
  if(u < 0 || u > 1) return 0;
  if(p->kind == 3) return 2 * A * (1 - fabsf(2 * u - 1)) * p->sgn;
  int n = (int)((t1 - t0) / (st->period * k->res) + 0.5f);
  if(n < 1) n = 1;
  float v = u * n - floorf(u * n);
  if(p->kind == 1) return A * v * p->sgn;   // Saw: slow ramp, sharp drop
  return (v > 0.25f && v < 0.75f ? A : 0) * p->sgn;
}

// Straight-edged cells around base relaxed random sites, some edges decorated. Returns 0, or -1.
static int sk_geobase(Sk * k, uint32_t base) {
  const SnakeStyle * st = k->st;
  int n = k->w * k->h, err = 0;
  float * s = malloc(sizeof(float) * 2 * base), * sum = malloc(sizeof(float) * 3 * base);
  int32_t * i2 = malloc(sizeof(int32_t) * n);
  if(!s || !sum || !i2) err = -1;
  float bs = sqrtf((float)n / base);
  for(uint32_t i = 0; i < base && !err; i++) {
    s[2 * i] = rng_range(&k->r, 0, k->w);
    s[2 * i + 1] = rng_range(&k->r, 0, k->h);
  }
  SkSites ss = {0};
  int passes = (int)st->relax;
  for(int pass = 0; pass <= passes && !err; pass++) {
    if(sk_sitesinit(&ss, s, base, k->w, k->h, bs)) { err = -1; break; }
    for(int i = 0; i < n; i++) sk_near2(&ss, i % k->w + 0.5f, i / k->w + 0.5f, &k->lab[i], &i2[i]);
    sk_sitesfree(&ss);
    if(pass == passes) break;
    memset(sum, 0, sizeof(float) * 3 * base);
    for(int i = 0; i < n; i++) { float * q = sum + 3 * k->lab[i]; q[0] += i % k->w + 0.5f; q[1] += i / k->w + 0.5f; q[2]++; }
    for(uint32_t i = 0; i < base; i++) if(sum[3 * i + 2] > 0) { s[2 * i] = sum[3 * i] / sum[3 * i + 2]; s[2 * i + 1] = sum[3 * i + 1] / sum[3 * i + 2]; }
  }
  // Edges: the site pairs whose cells touch, with their extent along the bisector
  SkPair * pr = err ? NULL : malloc(sizeof(SkPair) * 2 * n);
  int np = 0;
  if(!err && !pr) err = -1;
  for(int i = 0; i < n && !err; i++) {
    int x = i % k->w, y = i / k->w;
    int nb[2] = { x < k->w - 1 ? i + 1 : -1, y < k->h - 1 ? i + k->w : -1 };
    for(int j = 0; j < 2; j++) {
      if(nb[j] < 0 || k->lab[nb[j]] == k->lab[i]) continue;
      uint32_t a = k->lab[i], b = k->lab[nb[j]];
      if(a > b) { uint32_t tt = a; a = b; b = tt; }
      float t, d;
      sk_bis(s, a, b, x + 0.5f + (j == 0) * 0.5f, y + 0.5f + (j == 1) * 0.5f, &t, &d);
      pr[np++] = (SkPair) { (uint64_t)a << 32 | b, t, t, 0, 1 };
    }
  }
  if(!err) {
    qsort(pr, np, sizeof(SkPair), sk_cmppair);
    int m = 0;
    for(int i = 0; i < np; i++) {
      if(m && pr[m - 1].key == pr[i].key) {
        pr[m - 1].tmin = fminf(pr[m - 1].tmin, pr[i].tmin);
        pr[m - 1].tmax = fmaxf(pr[m - 1].tmax, pr[i].tmax);
      } else {
        pr[m++] = pr[i];
      }
    }
    np = m;
    for(int i = 0; i < np; i++) {
      float r = rng_range(&k->r, 0, 1);
      pr[i].kind = r < st->pstraight ? 0 : (r -= st->pstraight) < st->psaw ? 1 : (r -= st->psaw) < st->psquare ? 2 :
          (r -= st->psquare) < st->pchevron ? 3 : 4;
      pr[i].sgn = rng_below(&k->r, 2) ? 1 : -1;
    }
    // Final cells: a square near a decorated edge goes to whichever side of the moved line it is
    for(int i = 0; i < n; i++) {
      uint32_t a = k->lab[i], b = i2[i];
      if(a > b) { uint32_t tt = a; a = b; b = tt; }
      SkPair key = { (uint64_t)a << 32 | b, 0, 0, 0, 0 };
      const SkPair * p = bsearch(&key, pr, np, sizeof(SkPair), sk_cmppair);
      if(!p || !p->kind) continue;
      float t, d;
      sk_bis(s, a, b, i % k->w + 0.5f, i / k->w + 0.5f, &t, &d);
      k->lab[i] = d > sk_decor(k, p, t) ? (int32_t)a : (int32_t)b;
    }
  }
  free(s); free(sum); free(i2); free(pr);
  return err;
}

// One attempt with base stroke pieces: 0 done, 1 failed, -1 out of memory, or 2 + shortfall
static int sk_build(CutGraph * g, float W, float H, uint32_t count, uint32_t base, uint32_t seed, const SnakeStyle * st) {
  CutGraph sg;
  memset(&sg, 0, sizeof(CutGraph));
  if(!st->geo && strokecut_graphx(&sg, W, H, base, 0, 0, seed, 0)) return 1;
  Sk k;
  memset(&k, 0, sizeof(Sk));
  k.st = st;
  k.res = st->res;
  k.w = (int)ceilf(W * st->res);
  k.h = (int)ceilf(H * st->res);
  rng_init(&k.r, seed ^ 0x5AA4E5u);
  int n = k.w * k.h, err = 0;
  k.lab = malloc(sizeof(int32_t) * n);
  k.snk = calloc(n, sizeof(int32_t));
  k.mark = calloc(n, sizeof(int32_t));
  k.marks = malloc(sizeof(float) * n);
  k.body = malloc(sizeof(int32_t) * n);
  k.edge = malloc(sizeof(int32_t) * n);
  k.nlab = st->geo ? base : sg.count;
  k.sent = calloc(k.nlab, 1);
  float * xy = st->geo ? NULL : malloc(sizeof(float) * 2 * cutgraph_maxpoints(&sg));
  if(!k.lab || !k.snk || !k.mark || !k.marks || !k.body || !k.edge || !k.sent || (!st->geo && !xy)) err = -1;
  if(!err && st->geo) err = sk_geobase(&k, base);
  // Raster the stroke cut (square centres inside each outline)
  for(int i = 0; i < n && !err && !st->geo; i++) k.lab[i] = -2;
  for(uint32_t p = 0; p < sg.count && !err; p++) {
    uint32_t m = cutgraph_outline(&sg, p, k.res, 0, 0, xy);
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for(uint32_t j = 0; j < m; j++) {
      x0 = fminf(x0, xy[2 * j]); x1 = fmaxf(x1, xy[2 * j]);
      y0 = fminf(y0, xy[2 * j + 1]); y1 = fmaxf(y1, xy[2 * j + 1]);
    }
    int ix0 = (int)fmaxf(0, floorf(x0)), ix1 = (int)fminf(k.w - 1, ceilf(x1));
    int iy0 = (int)fmaxf(0, floorf(y0)), iy1 = (int)fminf(k.h - 1, ceilf(y1));
    for(int y = iy0; y <= iy1; y++)
      for(int x = ix0; x <= ix1; x++)
        if(geom_inside(xy, m, x + 0.5f, y + 0.5f)) k.lab[y * k.w + x] = p;
  }
  cutgraph_free(&sg);
  if(!err) {
    sk_absorb(&k);
    for(int i = 0; i < n; i++) if(sk_other4(&k, i, NULL)) k.edge[k.nedge++] = i;
    if(!k.nedge) err = 1;
  }
  // Snakes
  int want = (int)(count * st->snakes + 0.5f), placed = 0, pairs = 0;
  float * path = malloc(sizeof(float) * 2 * SK_PATHMAX);
  if(!path) err = -1;
  for(int t = 0; t < want * 150 && placed < want && !err; t++) {
    // Yin-yangs fit more easily than snakes: capped at their share of what's placed
    int pair = rng_range(&k.r, 0, 1) < st->pairs && pairs < st->pairs * want;
    int res = pair ? sk_pair(&k) : sk_snake(&k, path);
    if(res < 0) err = -1;
    else { placed += res; pairs += pair && res; }
  }
  free(path);
  float a0 = (float)n / count;
  if(!err) err = sk_thin(&k);
  if(!err) err = sk_parts(&k, a0);
#ifdef SNAKECUT_DEBUG
  if(!err) fprintf(stderr, "snakecut: base %u, snakes %d of %d, pieces after %u (want %u)\n", base, placed, want, k.nlab, count);
#endif
  if(!err) err = sk_merge(&k, count, st->tiny * a0);
  if(err == 1) err = 2 + (int)(count / 20);   // Short of pieces: start from more
  if(!err) {
    // Geometric: only lines touching snakes are smoothed (snake squares, grown by one)
    uint8_t * curvy = NULL;
    if(st->geo) {
      curvy = calloc(n, 1);
      if(!curvy) err = -1;
      for(int i = 0; i < n && curvy; i++) {
        if(!k.snk[i]) continue;
        int x = i % k.w, y = i / k.w;
        for(int dy = -1; dy <= 1; dy++)
          for(int dx = -1; dx <= 1; dx++)
            if(x + dx >= 0 && y + dy >= 0 && x + dx < k.w && y + dy < k.h) curvy[(y + dy) * k.w + x + dx] = 1;
      }
    }
    if(!err) err = pottscut_fromlabelsx(g, k.lab, k.w, k.h, count, k.res, st->smooth, NULL, seed, curvy, st->simplify) ? 1 : 0;
    free(curvy);
  }
  free(k.lab); free(k.snk); free(k.mark); free(k.marks); free(k.body); free(k.edge); free(k.sent); free(xy);
  return err;
}

int snakecut_graph(CutGraph * g, float width, float height, uint32_t count, uint8_t whimsy, uint8_t set,
    uint32_t seed, const SnakeStyle * st) {
  memset(g, 0, sizeof(CutGraph));
  (void)whimsy; (void)set;
  if(count < 2 || count > 0xFFF0 || !(width > 0) || !(height > 0)) return 1;
  uint32_t base = (uint32_t)(count * st->base + 0.5f);
  if(base < 2) base = 2;
  for(uint32_t t = 0; t < SNAKECUT_TRIES; t++) {
    int res = sk_build(g, width, height, count, base, seed ^ (t * 0x9E3779B1u), st);
    if(res == 0) {
      g->tries = t + 1;
      return 0;
    }
    if(res < 0) return 1;
    if(res >= 2) base += res - 2 + 1;   // Too few pieces: start from more
  }
  return 1;
}

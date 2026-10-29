#include "strokecut.h"
#include "geom.h"
#include "rng.h"
#include "whimsy.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef STROKECUT_DEBUG
#include <stdio.h>
#endif

// Regions are cyclic lists of point ids, clockwise (y down); neighbours share point ids along
// their common boundary (in opposite order). A whimsy not anchored yet is a hole: its region
// exists but no other region's list goes around it. Anchoring it (the first stroke) makes the
// host region walk out along the stroke, around the whimsy and back (a slit, both sides in one
// list) until the second stroke splits the host.

typedef struct {
  uint32_t * id;
  uint32_t n, cap;
  uint8_t whimsy;
  uint8_t stuck;    // No cut found: try other regions
  float area;
} ScReg;

typedef struct {
  float * xy;
  uint16_t * occ;   // Times each point appears in region lists
  uint8_t * snakept;  // Points on a snake's outline (sized like occ)
  uint32_t npt, cappt;
  ScReg * reg;
  uint32_t nreg, capreg;
  uint32_t * hole;  // Whimsy regions not anchored yet
  uint32_t nhole;
  float w, h;
  Rng r;
  float * buf;      // Scratch coordinates
  uint32_t capbuf;
} Sc;

// ---- Storage ----

static int64_t sc_addpt(Sc * s, float x, float y) {
  if(s->npt == s->cappt) {
    uint32_t cap = s->cappt ? s->cappt * 2 : 1024;
    float * xy = realloc(s->xy, sizeof(float) * 2 * cap);
    if(!xy) return -1;
    s->xy = xy;
    uint16_t * occ = realloc(s->occ, sizeof(uint16_t) * cap);
    if(!occ) return -1;
    s->occ = occ;
    uint8_t * sp = realloc(s->snakept, cap);
    if(!sp) return -1;
    s->snakept = sp;
    s->cappt = cap;
  }
  s->xy[2 * s->npt] = x;
  s->xy[2 * s->npt + 1] = y;
  s->occ[s->npt] = 0;
  s->snakept[s->npt] = 0;
  return s->npt++;
}

static int64_t sc_newreg(Sc * s, uint8_t whimsy) {
  if(s->nreg == s->capreg) {
    uint32_t cap = s->capreg ? s->capreg * 2 : 64;
    ScReg * reg = realloc(s->reg, sizeof(ScReg) * cap);
    if(!reg) return -1;
    s->reg = reg;
    s->capreg = cap;
  }
  memset(s->reg + s->nreg, 0, sizeof(ScReg));
  s->reg[s->nreg].whimsy = whimsy;
  return s->nreg++;
}

static int sc_reserve(ScReg * R, uint32_t n) {
  if(n <= R->cap) return 0;
  uint32_t cap = R->cap ? R->cap : 16;
  while(cap < n) cap *= 2;
  uint32_t * id = realloc(R->id, sizeof(uint32_t) * cap);
  if(!id) return 1;
  R->id = id;
  R->cap = cap;
  return 0;
}

static int sc_push(ScReg * R, uint32_t id) {
  if(sc_reserve(R, R->n + 1)) return 1;
  R->id[R->n++] = id;
  return 0;
}

// Insert id before position pos
static int sc_insertat(ScReg * R, uint32_t pos, uint32_t id) {
  if(sc_reserve(R, R->n + 1)) return 1;
  memmove(R->id + pos + 1, R->id + pos, sizeof(uint32_t) * (R->n - pos));
  R->id[pos] = id;
  R->n++;
  return 0;
}

static const float * sc_coords(Sc * s, const ScReg * R) {
  if(R->n > s->capbuf) {
    float * b = realloc(s->buf, sizeof(float) * 2 * R->n);
    if(!b) return NULL;
    s->buf = b;
    s->capbuf = R->n;
  }
  for(uint32_t i = 0; i < R->n; i++) {
    s->buf[2 * i] = s->xy[2 * R->id[i]];
    s->buf[2 * i + 1] = s->xy[2 * R->id[i] + 1];
  }
  return s->buf;
}

static void sc_recount(Sc * s) {
  memset(s->occ, 0, sizeof(uint16_t) * s->npt);
  for(uint32_t r = 0; r < s->nreg; r++) {
    for(uint32_t i = 0; i < s->reg[r].n; i++) s->occ[s->reg[r].id[i]]++;
  }
}

static void sc_setarea(Sc * s, uint32_t r) {
  const float * xy = sc_coords(s, s->reg + r);
  s->reg[r].area = xy ? fabsf(geom_area(xy, s->reg[r].n, NULL, NULL)) : 0;
}

// Put point id into every region boundary step a -> b or b -> a
static int sc_splitseg(Sc * s, uint32_t a, uint32_t b, uint32_t id) {
  for(uint32_t r = 0; r < s->nreg; r++) {
    ScReg * R = s->reg + r;
    for(uint32_t i = 0; i < R->n; i++) {
      uint32_t p = R->id[i], q = R->id[(i + 1) % R->n];
      if((p == a && q == b) || (p == b && q == a)) {
        if(sc_insertat(R, i + 1, id)) return 1;
        i++;
      }
    }
  }
  return 0;
}

static uint32_t sc_countin(const ScReg * R, uint32_t id) {
  uint32_t c = 0;
  for(uint32_t i = 0; i < R->n; i++) c += R->id[i] == id;
  return c;
}

// ---- Geometry ----

static void sc_pt(const Sc * s, uint32_t id, float * x, float * y) {
  *x = s->xy[2 * id];
  *y = s->xy[2 * id + 1];
}

// First crossing of ray o + t d (t > tmin) with R's boundary. Returns 0 if found.
static int sc_rayhit(const Sc * s, const ScReg * R, float ox, float oy, float dx, float dy, float tmin,
    float * tout, uint32_t * seg) {
  float best = 1e30f;
  int found = 0;
  for(uint32_t i = 0; i < R->n; i++) {
    float px, py, qx, qy;
    sc_pt(s, R->id[i], &px, &py);
    sc_pt(s, R->id[(i + 1) % R->n], &qx, &qy);
    float ex = qx - px, ey = qy - py;
    float den = dx * ey - dy * ex;
    if(fabsf(den) < 1e-12f) continue;
    float wx = px - ox, wy = py - oy;
    float t = (wx * ey - wy * ex) / den, u = (wx * dy - wy * dx) / den;
    if(u < 0 || u > 1 || t <= tmin || t >= best) continue;
    best = t;
    *seg = i;
    found = 1;
  }
  *tout = best;
  return !found;
}

// 1 if (x, y) is near a corner of R: a junction, or a point where the boundary turns sharply
static int sc_nearcorner(const Sc * s, const ScReg * R, float x, float y) {
  for(uint32_t i = 0; i < R->n; i++) {
    float px, py, ax, ay, bx, by;
    sc_pt(s, R->id[i], &px, &py);
    if(hypotf(px - x, py - y) >= STROKECUT_CORNER) continue;
    if(s->occ[R->id[i]] >= 3) return 1;
    sc_pt(s, R->id[(i + R->n - 1) % R->n], &ax, &ay);
    sc_pt(s, R->id[(i + 1) % R->n], &bx, &by);
    float ux = px - ax, uy = py - ay, vx = bx - px, vy = by - py;
    float lu = hypotf(ux, uy), lv = hypotf(vx, vy);
    if(lu < 1e-9f || lv < 1e-9f) return 1;
    float c = (ux * vx + uy * vy) / (lu * lv);
    if(c < cosf(STROKECUT_TURN)) return 1;
  }
  return 0;
}

// 1 if step i of R is one side of a slit (both its points are on R's boundary twice)
static int sc_slit(const ScReg * R, uint32_t i) {
  return sc_countin(R, R->id[i]) > 1 && sc_countin(R, R->id[(i + 1) % R->n]) > 1;
}

// Unit normal into R across step i (clockwise, y down: (-dy, dx) of the walk)
static void sc_inward(const Sc * s, const ScReg * R, uint32_t i, float * nx, float * ny) {
  float px, py, qx, qy;
  sc_pt(s, R->id[i], &px, &py);
  sc_pt(s, R->id[(i + 1) % R->n], &qx, &qy);
  float dx = qx - px, dy = qy - py, l = hypotf(dx, dy);
  *nx = -dy / l;
  *ny = dx / l;
}

// Interior samples of a smooth curve from A (leaving along na) to B (arriving against nb).
// Returns the count (at most max).
static uint32_t sc_curve(Rng * r, float ax, float ay, float nax, float nay, float bx, float by, float nbx, float nby,
    float shape, float * out, uint32_t max) {
  float D = hypotf(bx - ax, by - ay);
  float px = -(by - ay) / D, py = (bx - ax) / D;
  float k1 = rng_range(r, 0.3f, 0.45f) * D, k2 = rng_range(r, 0.3f, 0.45f) * D;
  float s1 = rng_range(r, -shape, shape) * D, s2 = rng_range(r, -shape, shape) * D;
  float c1x = ax + nax * k1 + px * s1, c1y = ay + nay * k1 + py * s1;
  float c2x = bx + nbx * k2 + px * s2, c2y = by + nby * k2 + py * s2;
  float approx = hypotf(c1x - ax, c1y - ay) + hypotf(c2x - c1x, c2y - c1y) + hypotf(bx - c2x, by - c2y);
  uint32_t m = (uint32_t)(approx / STROKECUT_STEP) + 1;
  if(m < 4) m = 4;
  if(m > max + 1) m = max + 1;
  for(uint32_t i = 1; i < m; i++) {
    float t = (float)i / m, u = 1 - t;
    float w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
    out[2 * (i - 1)] = w0 * ax + w1 * c1x + w2 * c2x + w3 * bx;
    out[2 * (i - 1) + 1] = w0 * ay + w1 * c1y + w2 * c2y + w3 * by;
  }
  return m - 1;
}

// 1 if the curve samples (cv, m) keep STROKECUT_CLEAR from region L's boundary, except
// near its own ends A and B (where it meets the lines)
static int sc_clearof(const Sc * s, const ScReg * L, const float * cv, uint32_t m, float ax, float ay, float bx, float by) {
  float cl = STROKECUT_CLEAR, near = 2.5f * STROKECUT_CLEAR;
  for(uint32_t i = 0; i < L->n; i++) {
    float px, py, qx, qy;
    sc_pt(s, L->id[i], &px, &py);
    sc_pt(s, L->id[(i + 1) % L->n], &qx, &qy);
    int nearA = geom_pointseg(ax, ay, px, py, qx, qy) < near, nearB = geom_pointseg(bx, by, px, py, qx, qy) < near;
    for(uint32_t k = 0; k < m; k++) {
      float x = cv[2 * k], y = cv[2 * k + 1];
      if(nearA && hypotf(x - ax, y - ay) < near) continue;
      if(nearB && hypotf(x - bx, y - by) < near) continue;
      if(geom_pointseg(x, y, px, py, qx, qy) < cl) return 0;
    }
  }
  return 1;
}

static int sc_holesclear(const Sc * s, const float * cv, uint32_t m, float ax, float ay, float bx, float by) {
  for(uint32_t h = 0; h < s->nhole; h++) {
    if(!sc_clearof(s, s->reg + s->hole[h], cv, m, ax, ay, bx, by)) return 0;
  }
  return 1;
}

static int sc_hasdup(const ScReg * R) {
  for(uint32_t i = 0; i < R->n; i++) {
    for(uint32_t j = i + 1; j < R->n; j++) {
      if(R->id[i] == R->id[j]) return 1;
    }
  }
  return 0;
}

// ---- Cuts ----

// Cut region ri from A (on step sa) to B (on step sb) along a fresh curve, if it keeps clear
// and each half gets at least share of the area (and, without slits, is thick enough).
// Returns 0 if done, 1 if not (nothing changed), -1 out of memory.
static int sc_cut(Sc * s, uint32_t ri, float ax, float ay, uint32_t sa, float bx, float by, uint32_t sb, float share,
    float shape) {
  ScReg * R = s->reg + ri;
  float nax, nay, nbx, nby;
  sc_inward(s, R, sa, &nax, &nay);
  sc_inward(s, R, sb, &nbx, &nby);
  float cv[2 * 512];
  uint32_t m = sc_curve(&s->r, ax, ay, nax, nay, bx, by, nbx, nby, shape, cv, 512);
  if(!sc_clearof(s, R, cv, m, ax, ay, bx, by) || !sc_holesclear(s, cv, m, ax, ay, bx, by)) return 1;
  const float * xy = sc_coords(s, R);
  if(!xy) return -1;
  if(!geom_inside(xy, R->n, cv[2 * (m / 2)], cv[2 * (m / 2) + 1])) return 1;
  // The two halves (as coordinates) to check before changing anything:
  // half1 = A, R after sa .. R up to sb, B, curve back; half2 = B, R after sb .. up to sa, A, curve
  uint32_t n1 = (sb + R->n - sa) % R->n, n2 = R->n - n1;
  if(sa == sb) return 1;
  float * h = malloc(sizeof(float) * 2 * (R->n + 2 * m + 4));
  if(!h) return -1;
  uint32_t k = 0;
  h[k++] = ax; h[k++] = ay;
  for(uint32_t i = 1; i <= n1; i++) { h[k++] = xy[2 * ((sa + i) % R->n)]; h[k++] = xy[2 * ((sa + i) % R->n) + 1]; }
  h[k++] = bx; h[k++] = by;
  for(uint32_t i = m; i-- > 0; ) { h[k++] = cv[2 * i]; h[k++] = cv[2 * i + 1]; }
  uint32_t c1 = k / 2;
  float a1 = fabsf(geom_area(h, c1, NULL, NULL));
  int ok = a1 >= share * R->area && R->area - a1 >= share * R->area;
  int dup = sc_hasdup(R);
  if(ok && !dup) ok = cutgraph_thick_ok(h, c1, STROKECUT_CLEAR);
  if(ok && !dup) {
    k = 0;
    h[k++] = bx; h[k++] = by;
    for(uint32_t i = 1; i <= n2; i++) { h[k++] = xy[2 * ((sb + i) % R->n)]; h[k++] = xy[2 * ((sb + i) % R->n) + 1]; }
    h[k++] = ax; h[k++] = ay;
    for(uint32_t i = 0; i < m; i++) { h[k++] = cv[2 * i]; h[k++] = cv[2 * i + 1]; }
    ok = cutgraph_thick_ok(h, k / 2, STROKECUT_CLEAR);
  }
  free(h);
  if(!ok) return 1;
  // Apply: new points on the steps (and on the neighbours' matching steps), then two lists
  uint32_t pa = R->id[sa], qa = R->id[(sa + 1) % R->n], pb = R->id[sb], qb = R->id[(sb + 1) % R->n];
  int64_t ida = sc_addpt(s, ax, ay), idb = sc_addpt(s, bx, by);
  if(ida < 0 || idb < 0) return -1;
  int64_t first = s->npt;
  for(uint32_t i = 0; i < m; i++) if(sc_addpt(s, cv[2 * i], cv[2 * i + 1]) < 0) return -1;
  R = s->reg + ri;
  if(sc_splitseg(s, pa, qa, ida) || sc_splitseg(s, pb, qb, idb)) return -1;
  R = s->reg + ri;
  // Positions of A and B in R (unique: the cut steps aren't slit sides)
  uint32_t ia = 0, ib = 0;
  for(uint32_t i = 0; i < R->n; i++) {
    if(R->id[i] == (uint32_t)ida) ia = i;
    if(R->id[i] == (uint32_t)idb) ib = i;
  }
  int64_t nr = sc_newreg(s, 0);
  if(nr < 0) return -1;
  R = s->reg + ri;
  ScReg half1 = {0}, half2 = {0};
  int err = 0;
  for(uint32_t i = ia; !err; i = (i + 1) % R->n) {
    err |= sc_push(&half1, R->id[i]);
    if(i == ib) break;
  }
  for(uint32_t i = m; i-- > 0 && !err; ) err |= sc_push(&half1, first + i);
  for(uint32_t i = ib; !err; i = (i + 1) % R->n) {
    err |= sc_push(&half2, R->id[i]);
    if(i == ia) break;
  }
  for(uint32_t i = 0; i < m && !err; i++) err |= sc_push(&half2, first + i);
  if(err) {
    free(half1.id);
    free(half2.id);
    return -1;
  }
  free(R->id);
  R->id = half1.id; R->n = half1.n; R->cap = half1.cap; R->stuck = 0;
  ScReg * N = s->reg + nr;
  N->id = half2.id; N->n = half2.n; N->cap = half2.cap;
  sc_setarea(s, ri);
  sc_setarea(s, nr);
  sc_recount(s);
  return 0;
}

// Cut the region in two across its long axis, near its middle; if that keeps failing (odd
// shapes), from random points inside in random directions, less evenly
static int sc_split(Sc * s, uint32_t ri) {
  for(int t = 0; t < 2 * STROKECUT_TRIES; t++) {
    int wild = t >= STROKECUT_TRIES;
    ScReg * R = s->reg + ri;
    const float * xy = sc_coords(s, R);
    if(!xy) return -1;
    float cx, cy;
    geom_area(xy, R->n, &cx, &cy);
    // Long axis from the boundary's spread (points weighted by their steps)
    float sxx = 0, syy = 0, sxy = 0, wsum = 0;
    for(uint32_t i = 0; i < R->n; i++) {
      uint32_t j = (i + 1) % R->n;
      float mx = (xy[2 * i] + xy[2 * j]) / 2 - cx, my = (xy[2 * i + 1] + xy[2 * j + 1]) / 2 - cy;
      float w = hypotf(xy[2 * j] - xy[2 * i], xy[2 * j + 1] - xy[2 * i + 1]);
      sxx += w * mx * mx; syy += w * my * my; sxy += w * mx * my; wsum += w;
    }
    float phi = 0.5f * atan2f(2 * sxy, sxx - syy);
    float ext = sqrtf((sxx + syy) / (wsum > 0 ? wsum : 1)) * 2;
    float off = rng_range(&s->r, -0.15f, 0.15f) * ext;
    float ox = cx + cosf(phi) * off, oy = cy + sinf(phi) * off;
    float th = phi + (float)M_PI / 2 + rng_range(&s->r, -0.45f, 0.45f);
    if(wild) {
      float x0 = xy[0], y0 = xy[1], x1 = xy[0], y1 = xy[1];
      for(uint32_t i = 1; i < R->n; i++) {
        x0 = fminf(x0, xy[2 * i]); x1 = fmaxf(x1, xy[2 * i]);
        y0 = fminf(y0, xy[2 * i + 1]); y1 = fmaxf(y1, xy[2 * i + 1]);
      }
      ox = rng_range(&s->r, x0, x1);
      oy = rng_range(&s->r, y0, y1);
      th = phi + (float)M_PI / 2 + rng_range(&s->r, -1.2f, 1.2f);
    }
    float dx = cosf(th), dy = sinf(th);
    if(!geom_inside(xy, R->n, ox, oy)) continue;
    float ta, tb;
    uint32_t sa, sb;
    if(sc_rayhit(s, R, ox, oy, dx, dy, 1e-6f, &ta, &sa) || sc_rayhit(s, R, ox, oy, -dx, -dy, 1e-6f, &tb, &sb)) continue;
    float ax = ox + dx * ta, ay = oy + dy * ta, bx = ox - dx * tb, by = oy - dy * tb;
    if(sc_nearcorner(s, R, ax, ay) || sc_nearcorner(s, R, bx, by) || sc_slit(R, sa) || sc_slit(R, sb)) continue;
    int res = sc_cut(s, ri, ax, ay, sa, bx, by, sb, wild ? STROKECUT_BALANCE / 2 : STROKECUT_BALANCE, STROKECUT_SHAPE);
    if(res <= 0) return res;
  }
  return 1;
}

// ---- Whimsies ----

// Place whimsy shapes (as regions, all holes at first). Returns the number placed, -1 out of memory.
static int sc_placewhimsies(Sc * s, uint32_t want, uint8_t set) {
  float base[WHIMSY_MAXSHAPE * 2];
  float * cx = malloc(sizeof(float) * 3 * (want ? want : 1));
  uint32_t * hole = malloc(sizeof(uint32_t) * (want ? want : 1));
  if(!cx || !hole) {
    free(cx);
    free(hole);
    return -1;
  }
  s->hole = hole;
  uint32_t placed = 0, nshapes = whimsy_setsize(set), first = rng_below(&s->r, nshapes);
  for(uint32_t k = 0; k < want; k++) {
    uint32_t n = whimsy_shape(set, first + k, base);
    float rad = 0;
    for(uint32_t i = 0; i < n; i++) rad = fmaxf(rad, hypotf(base[2 * i], base[2 * i + 1]));
    for(int t = 0; t < WHIMSY_TRIES; t++) {
      float sc = sqrtf(WHIMSY_AREA * rng_range(&s->r, 0.85f, 1.15f));
      float ang = rng_range(&s->r, -0.35f, 0.35f), ca = cosf(ang), sa = sinf(ang);
      float R = rad * sc, lo = WHIMSY_BORDER + R;
      if(s->w - 2 * lo <= 0 || s->h - 2 * lo <= 0) break;
      float x = rng_range(&s->r, lo, s->w - lo), y = rng_range(&s->r, lo, s->h - lo);
      int close = 0;
      for(uint32_t j = 0; j < placed; j++) {
        // Room between whimsies for a stroke and pieces
        if(hypotf(x - cx[3 * j], y - cx[3 * j + 1]) < R + cx[3 * j + 2] + 2 * WHIMSY_GAP) close = 1;
      }
      if(close) continue;
      int64_t ri = sc_newreg(s, 1);
      if(ri < 0) { free(cx); return -1; }
      for(uint32_t i = 0; i < n; i++) {
        float px = base[2 * i] * sc, py = base[2 * i + 1] * sc;
        int64_t id = sc_addpt(s, x + px * ca - py * sa, y + px * sa + py * ca);
        if(id < 0 || sc_push(s->reg + ri, id)) { free(cx); return -1; }
      }
      sc_setarea(s, ri);
      cx[3 * placed] = x;
      cx[3 * placed + 1] = y;
      cx[3 * placed + 2] = R;
      s->hole[s->nhole++] = ri;
      placed++;
      break;
    }
  }
  free(cx);
  sc_recount(s);
  return placed;
}

// Region (not a whimsy) whose area contains (x, y), or -1
static int64_t sc_regionat(Sc * s, float x, float y) {
  for(uint32_t r = 0; r < s->nreg; r++) {
    if(s->reg[r].whimsy) continue;
    const float * xy = sc_coords(s, s->reg + r);
    if(xy && geom_inside(xy, s->reg[r].n, x, y)) return r;
  }
  return -1;
}

// Anchor hole h: a stroke from the whimsy out to its host region's boundary (the host then
// walks around the whimsy through a slit), then a second one leaving the other way, which
// splits the host. Returns 0 if done, 1 if not, -1 out of memory.
// One go at it; on failure the first stroke may be in already (sc_anchor undoes it)
static int sc_anchoronce(Sc * s, uint32_t wi, uint32_t hi, float wx, float wy) {
  // Toward the nearest border first (the shortest way out), then anywhere
  float bd[4] = { wx, s->w - wx, wy, s->h - wy }, ba[4] = { (float)M_PI, 0, -(float)M_PI / 2, (float)M_PI / 2 };
  int nb = 0;
  for(int k = 1; k < 4; k++) if(bd[k] < bd[nb]) nb = k;
  for(int t = 0; t < STROKECUT_TRIES; t++) {
    ScReg * W = s->reg + wi, * R = s->reg + hi;
    float th = t < STROKECUT_TRIES / 2 ? ba[nb] + rng_range(&s->r, -0.8f, 0.8f) : rng_range(&s->r, 0, 2 * (float)M_PI);
    float dx = cosf(th), dy = sinf(th);
    float ta, tb;
    uint32_t sa, sb;
    if(sc_rayhit(s, W, wx, wy, dx, dy, 1e-6f, &ta, &sa)) continue;
    float ax = wx + dx * ta, ay = wy + dy * ta;
    if(sc_rayhit(s, R, ax, ay, dx, dy, 1e-5f, &tb, &sb)) continue;
    float bx = ax + dx * tb, by = ay + dy * tb;
    // Nothing else (another whimsy) in the way
    int blocked = 0;
    for(uint32_t k = 0; k < s->nhole && !blocked; k++) {
      float tk;
      uint32_t sk;
      if(!sc_rayhit(s, s->reg + s->hole[k], ax, ay, dx, dy, 1e-5f, &tk, &sk) && tk < tb) blocked = 1;
    }
    if(blocked || sc_nearcorner(s, W, ax, ay) || sc_nearcorner(s, R, bx, by) || sc_slit(R, sb)) continue;
    // Leave the whimsy square on (outward = minus its inward normal)
    float nax, nay, nbx, nby;
    sc_inward(s, W, sa, &nax, &nay);
    nax = -nax; nay = -nay;
    sc_inward(s, R, sb, &nbx, &nby);
    float cv[2 * 512];
    uint32_t m = sc_curve(&s->r, ax, ay, nax, nay, bx, by, nbx, nby, STROKECUT_SHAPE / 2, cv, 512);
    if(!sc_clearof(s, R, cv, m, ax, ay, bx, by) || !sc_clearof(s, W, cv, m, ax, ay, bx, by) ||
        !sc_holesclear(s, cv, m, ax, ay, bx, by)) continue;
    // Apply the first stroke: host goes ... B, stroke back to A, around the whimsy
    // (backwards: it's a hole), A, stroke to B, B ...
    uint32_t wp = W->id[sa], wq = W->id[(sa + 1) % W->n], rp = R->id[sb], rq = R->id[(sb + 1) % R->n];
    int64_t ida = sc_addpt(s, ax, ay), idb = sc_addpt(s, bx, by);
    if(ida < 0 || idb < 0) return -1;
    int64_t first = s->npt;
    for(uint32_t i = 0; i < m; i++) if(sc_addpt(s, cv[2 * i], cv[2 * i + 1]) < 0) return -1;
    if(sc_splitseg(s, wp, wq, ida) || sc_splitseg(s, rp, rq, idb)) return -1;
    W = s->reg + wi;
    R = s->reg + hi;
    uint32_t ka = 0, ib = 0;
    for(uint32_t i = 0; i < W->n; i++) if(W->id[i] == (uint32_t)ida) ka = i;
    for(uint32_t i = 0; i < R->n; i++) if(R->id[i] == (uint32_t)idb) ib = i;
    ScReg nl = {0};
    int err = 0;
    for(uint32_t i = 0; i <= ib && !err; i++) err |= sc_push(&nl, R->id[i]);
    for(uint32_t i = m; i-- > 0 && !err; ) err |= sc_push(&nl, first + i);
    for(uint32_t j = 0; j <= W->n && !err; j++) err |= sc_push(&nl, W->id[(ka + W->n - j) % W->n]);
    for(uint32_t i = 0; i < m && !err; i++) err |= sc_push(&nl, first + i);
    for(uint32_t i = ib; i < R->n && !err; i++) err |= sc_push(&nl, R->id[i]);
    if(err) { free(nl.id); return -1; }
    free(R->id);
    R->id = nl.id; R->n = nl.n; R->cap = nl.cap;
    sc_setarea(s, hi);
    sc_recount(s);
    // Second stroke: leaving well apart from the first (roughly the other way at first)
    for(int u = 0; u < 2 * STROKECUT_TRIES; u++) {
      R = s->reg + hi;
      W = s->reg + wi;
      float spread = u < STROKECUT_TRIES / 2 ? 0.7f : (float)M_PI - 1.2f;
      float th2 = th + (float)M_PI + rng_range(&s->r, -spread, spread), ex = cosf(th2), ey = sinf(th2);
      float t1, t2;
      uint32_t s1, s2;
      if(sc_rayhit(s, R, wx, wy, ex, ey, 1e-6f, &t1, &s1)) continue;
      float a2x = wx + ex * t1, a2y = wy + ey * t1;
      if(sc_rayhit(s, R, a2x, a2y, ex, ey, 1e-5f, &t2, &s2)) continue;
      float b2x = a2x + ex * t2, b2y = a2y + ey * t2;
      // A2 must be on the whimsy, B2 not (and neither on the slit)
      int onw1 = sc_countin(W, R->id[s1]) && sc_countin(W, R->id[(s1 + 1) % R->n]);
      int onw2 = sc_countin(W, R->id[s2]) && sc_countin(W, R->id[(s2 + 1) % R->n]);
      if(!onw1 || onw2 || sc_slit(R, s1) || sc_slit(R, s2)) continue;
      if(sc_nearcorner(s, R, a2x, a2y) || sc_nearcorner(s, R, b2x, b2y)) continue;
      int res = sc_cut(s, hi, a2x, a2y, s1, b2x, b2y, s2, 0.1f, STROKECUT_SHAPE / 2);
      if(res <= 0) return res;
    }
    return 1;   // The host keeps its slit (undone by the caller)
  }
  return 1;
}

// Region lists and point count, to undo a failed anchor
typedef struct {
  ScReg * reg;
  uint32_t nreg, npt;
} ScSnap;

static void sc_snapfree(ScSnap * k) {
  for(uint32_t r = 0; r < k->nreg; r++) free(k->reg[r].id);
  free(k->reg);
  k->reg = NULL;
}

static int sc_snap(const Sc * s, ScSnap * k) {
  k->nreg = s->nreg;
  k->npt = s->npt;
  k->reg = calloc(s->nreg ? s->nreg : 1, sizeof(ScReg));
  if(!k->reg) return 1;
  for(uint32_t r = 0; r < s->nreg; r++) {
    k->reg[r] = s->reg[r];
    k->reg[r].id = malloc(sizeof(uint32_t) * (s->reg[r].cap ? s->reg[r].cap : 1));
    if(!k->reg[r].id) { k->nreg = r; sc_snapfree(k); return 1; }
    memcpy(k->reg[r].id, s->reg[r].id, sizeof(uint32_t) * s->reg[r].n);
  }
  return 0;
}

// Put the snapshot back (it's used up)
static void sc_restore(Sc * s, ScSnap * k) {
  for(uint32_t r = 0; r < s->nreg; r++) free(s->reg[r].id);
  memcpy(s->reg, k->reg, sizeof(ScReg) * k->nreg);
  s->nreg = k->nreg;
  s->npt = k->npt;
  free(k->reg);
  k->reg = NULL;
  sc_recount(s);
}

// Anchor hole h (see sc_anchoronce), all or nothing; a whimsy that can't be anchored is
// dropped (fewer whimsies, as when one finds no spot). Returns 0 if done (or dropped), -1 out of memory.
static int sc_anchor(Sc * s, uint32_t h) {
  uint32_t wi = s->hole[h];
  const float * wxy = sc_coords(s, s->reg + wi);
  if(!wxy) return -1;
  float wx, wy;
  geom_area(wxy, s->reg[wi].n, &wx, &wy);
  // No longer a hole for the clearance checks of its own strokes (they start on it)
  s->hole[h] = s->hole[--s->nhole];
  int64_t hi = sc_regionat(s, wx, wy);
  for(int t = 0; t < STROKECUT_ANCHORS && hi >= 0; t++) {
    ScSnap k;
    if(sc_snap(s, &k)) return -1;
    int res = sc_anchoronce(s, wi, hi, wx, wy);
    if(res <= 0) {
      sc_snapfree(&k);
      return res;
    }
    sc_restore(s, &k);
  }
  // Drop it: its region goes (the last region takes its place)
  uint32_t last = s->nreg - 1;
  free(s->reg[wi].id);
  s->reg[wi] = s->reg[last];
  s->nreg--;
  for(uint32_t k = 0; k < s->nhole; k++) if(s->hole[k] == last) s->hole[k] = wi;
  sc_recount(s);
  return 0;
}

// ---- Snakes ----

// A junction: a point on 3+ boundaries (the puzzle border counts), or a puzzle corner
static int sc_junction(const Sc * s, uint32_t id) {
  float x = s->xy[2 * id], y = s->xy[2 * id + 1];
  int bx = x <= 1e-5f || x >= s->w - 1e-5f, by = y <= 1e-5f || y >= s->h - 1e-5f;
  return (bx && by) || s->occ[id] + (bx || by) >= 3;
}

// Region other than a holding step q -> p (the far side of a's step p -> q), or -1
static int64_t sc_across(const Sc * s, uint32_t a, uint32_t p, uint32_t q, uint32_t * at) {
  for(uint32_t r = 0; r < s->nreg; r++) {
    if(r == a) continue;
    const ScReg * R = s->reg + r;
    for(uint32_t i = 0; i < R->n; i++) {
      if(R->id[i] == q && R->id[(i + 1) % R->n] == p) { *at = i; return r; }
    }
  }
  return -1;
}

// Smallest distance from (x, y) to R's boundary
static float sc_bdist(const Sc * s, const ScReg * R, float x, float y) {
  float best = 1e30f;
  for(uint32_t i = 0; i < R->n; i++) {
    float px, py, qx, qy;
    sc_pt(s, R->id[i], &px, &py);
    sc_pt(s, R->id[(i + 1) % R->n], &qx, &qy);
    best = fminf(best, geom_pointseg(x, y, px, py, qx, qy));
  }
  return best;
}

#ifdef STROKECUT_DEBUG
static long sc_dbg[8];   // Snake: point too close, crossing, area, thin a, thin b, ok, candidates, eligible
#endif

// How sharply R's boundary turns at its point i (radians, 0 = straight on)
static float sc_turn(const Sc * s, const ScReg * R, uint32_t i) {
  float px, py, ax, ay, bx, by;
  sc_pt(s, R->id[i], &px, &py);
  sc_pt(s, R->id[(i + R->n - 1) % R->n], &ax, &ay);
  sc_pt(s, R->id[(i + 1) % R->n], &bx, &by);
  float ux = px - ax, uy = py - ay, vx = bx - px, vy = by - py;
  float c = (ux * vx + uy * vy) / (hypotf(ux, uy) * hypotf(vx, vy) + 1e-12f);
  return acosf(c > 1 ? 1 : c < -1 ? -1 : c);
}

typedef struct {
  uint32_t a, u, v;     // Region and the junctions the edge runs between (found again when it's
                        // its turn: earlier snakes change the lists)
} ScSnake;

// A finger of a snaking into b through the edge (a's chain ia .. ia + k, whose far side is
// region b at ib): along a smooth line through a few points inside b, round tip. A short
// edge becomes the finger whole (as wide as the edge); a longer one keeps its ends and grows
// the finger from a stretch in between, through a quickly narrowing neck. Returns 0 if
// done, 1 if not (nothing changed), -1 out of memory.
static int sc_snake(Sc * s, uint32_t a, uint32_t ia, uint32_t k, uint32_t b, uint32_t ib, float sc) {
  ScReg * A = s->reg + a, * B = s->reg + b;
  // The chain's points and distances along it
  float cx[STROKECUT_SNAKECHAIN], cy[STROKECUT_SNAKECHAIN], ca[STROKECUT_SNAKECHAIN];
  if(k + 1 > STROKECUT_SNAKECHAIN) return 1;
  for(uint32_t i = 0; i <= k; i++) {
    sc_pt(s, A->id[(ia + i) % A->n], cx + i, cy + i);
    ca[i] = i ? ca[i - 1] + hypotf(cx[i] - cx[i - 1], cy[i] - cy[i - 1]) : 0;
  }
  float L = ca[k];
  int whole = L <= STROKECUT_SNAKEMAX * sc;
  float s0, wb, w;   // Base middle (along the chain), base width, finger width
  if(whole) {
    // Only where b's boundary runs straight on through both ends (the strip's sides end on
    // a line of b's): where b has a corner (the edge is a T's stem), a full-width finger
    // would hug b's own edge
    if(sc_turn(s, B, ib) > STROKECUT_SNAKESTRAIGHT || sc_turn(s, B, (ib + k) % B->n) > STROKECUT_SNAKESTRAIGHT) {
#ifdef STROKECUT_DEBUG
      sc_dbg[0]++;
#endif
      return 1;
    }
    s0 = L / 2;
    wb = w = hypotf(cx[k] - cx[0], cy[k] - cy[0]);
  } else {
    w = rng_range(&s->r, STROKECUT_NECKMIN, STROKECUT_NECKMAX) * sc;
    wb = w + 2 * STROKECUT_NECKR * sc;   // Rounded into the edge on both sides
    float room = L - 2 * STROKECUT_CORNER * sc - wb;
    if(room < 0) return 1;
    s0 = STROKECUT_CORNER * sc + wb / 2 + rng_range(&s->r, 0, room);
  }
  // Base ends u, v on the chain (the chain's own ends for a whole edge) and the chain points
  // kept before / after them: chain[0 .. j0] u ... v chain[j1 .. k]
  float ux, uy, vx, vy;
  int32_t j0, j1;
  if(whole) {
    ux = cx[0]; uy = cy[0]; vx = cx[k]; vy = cy[k];
    j0 = -1;
    j1 = k + 1;
  } else {
    float su = s0 - wb / 2, sv = s0 + wb / 2;
    j0 = 0;
    while(j0 + 1 <= (int32_t)k && ca[j0 + 1] < su) j0++;
    j1 = k;
    while(j1 - 1 >= 0 && ca[j1 - 1] > sv) j1--;
    float f = (su - ca[j0]) / (ca[j0 + 1] - ca[j0]);
    ux = cx[j0] + (cx[j0 + 1] - cx[j0]) * f; uy = cy[j0] + (cy[j0 + 1] - cy[j0]) * f;
    f = (sv - ca[j1 - 1]) / (ca[j1] - ca[j1 - 1]);
    vx = cx[j1 - 1] + (cx[j1] - cx[j1 - 1]) * f; vy = cy[j1 - 1] + (cy[j1] - cy[j1 - 1]) * f;
  }
  float bl = hypotf(vx - ux, vy - uy), mx = (ux + vx) / 2, my = (uy + vy) / 2;
  float nx = (vy - uy) / bl, ny = -(vx - ux) / bl;   // Into b (a walks u -> v clockwise)
  // Control points: on into b, then snaking side to side
  float cp[2 * (STROKECUT_SNAKEPTS + 2)];
  cp[0] = mx - nx * 0.3f; cp[1] = my - ny * 0.3f;   // Phantom before the start (start heading)
  cp[2] = mx; cp[3] = my;
  // Steer: each step tries a spread of turns and takes the roomiest, preferring to turn the
  // other way from last time (so it snakes); stops when nothing has room
  float dx = nx, dy = ny, side = rng_range(&s->r, 0, 1) < 0.5f ? -1.0f : 1.0f;
  int npts = 0, want = 2 + (int)rng_below(&s->r, STROKECUT_SNAKEPTS - 1);
  for(int i = 0; i < want; i++) {
    float bestscore = -1e30f, bx = 0, by = 0, bdx = 0, bdy = 0;
    const float * bxy = sc_coords(s, B);
    if(!bxy) return -1;
    for(int c = 0; c < 9; c++) {
      float turn = i == 0 ? (c - 4) * 0.06f : (c - 4) * 0.3f;
      float co = cosf(turn), sn = sinf(turn), ndx = dx * co - dy * sn, ndy = dx * sn + dy * co;
      float len = rng_range(&s->r, STROKECUT_SNAKESTEP * 0.8f, STROKECUT_SNAKESTEP * 1.2f) * sc;
      float px = cp[2 * (i + 1)] + ndx * len, py = cp[2 * (i + 1) + 1] + ndy * len;
      float room = sc_bdist(s, B, px, py) - w / 2;
      if(room < STROKECUT_SNAKEMARGIN * sc || !geom_inside(bxy, B->n, px, py)) continue;
      float score = room + (i > 0 && turn * side > 0 ? 0.15f : 0) + rng_range(&s->r, 0, 0.1f);
      if(score > bestscore) { bestscore = score; bx = px; by = py; bdx = ndx; bdy = ndy; }
    }
    if(bestscore < -1e29f) break;
    side = -side;
    dx = bdx; dy = bdy;
    cp[2 * (i + 2)] = bx;
    cp[2 * (i + 2) + 1] = by;
    npts++;
  }
  if(npts < 1) {
#ifdef STROKECUT_DEBUG
    sc_dbg[0]++;
#endif
    return 1;
  }
  int ncp = npts + 2;
  // Centerline: Catmull-Rom through cp[1 ..], sampled finely
  float cl[2 * 400];
  int nc = 0;
  for(int i = 1; i < ncp - 1 && nc < 390; i++) {
    const float * p0 = cp + 2 * (i - 1), * p1 = cp + 2 * i, * p2 = cp + 2 * (i + 1);
    float p3x = i + 2 < ncp ? cp[2 * (i + 2)] : 2 * p2[0] - p1[0];
    float p3y = i + 2 < ncp ? cp[2 * (i + 2) + 1] : 2 * p2[1] - p1[1];
    // Finely at the start, where the neck rounds into the edge
    int m = (int)(hypotf(p2[0] - p1[0], p2[1] - p1[1]) / (i == 1 ? 0.012f : 0.04f)) + 2;
    for(int j = 0; j < m && nc < 390; j++) {
      float t = (float)j / m, t2 = t * t, t3 = t2 * t;
      cl[2 * nc] = 0.5f * (2 * p1[0] + (-p0[0] + p2[0]) * t + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3x) * t2 +
          (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3x) * t3);
      cl[2 * nc + 1] = 0.5f * (2 * p1[1] + (-p0[1] + p2[1]) * t + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3y) * t2 +
          (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3y) * t3);
      nc++;
    }
  }
  cl[2 * nc] = cp[2 * (ncp - 1)];
  cl[2 * nc + 1] = cp[2 * (ncp - 1) + 1];
  nc++;
  // A bend tighter than the finger's half width folds its inner side over itself
  for(int i = 1; i + 1 < nc; i++) {
    float ax = cl[2 * i] - cl[2 * i - 2], ay = cl[2 * i + 1] - cl[2 * i - 1];
    float bx = cl[2 * i + 2] - cl[2 * i], by = cl[2 * i + 3] - cl[2 * i + 1];
    float la = hypotf(ax, ay), lb = hypotf(bx, by);
    if(la < 1e-6f || lb < 1e-6f) continue;
    float ang = fabsf(atan2f(ax * by - ay * bx, ax * bx + ay * by));
    float maxw = w;
    if(ang > 1e-4f && (la + lb) / 2 / ang < maxw / 2 * 1.3f) {
#ifdef STROKECUT_DEBUG
      sc_dbg[1]++;
#endif
      return 1;
    }
  }
  // Sides (half the finger's width, tapering), the neck, then the round tip
  float total = 0;
  for(int i = 1; i < nc; i++) total += hypotf(cl[2 * i] - cl[2 * i - 2], cl[2 * i + 1] - cl[2 * i - 1]);
  int capn = 8, nf = 8, nd = 2 * (nc - 1) + capn - 1 + 2 * nf;
  float * det = malloc(sizeof(float) * 2 * (nd + 2));
  if(!det) return -1;
  float acc = 0, ex = 0, ey = 0, hw = w / 2, neck = (bl - w) / 2;   // Fillet radius (0 = no neck)
  float lx[400 * 2], rx[400 * 2], tg[400 * 2];
  int iR = 0;   // First centerline sample past the neck
  for(int i = 0; i < nc; i++) {
    if(i > 0) acc += hypotf(cl[2 * i] - cl[2 * i - 2], cl[2 * i + 1] - cl[2 * i - 1]);
    if(acc < neck) iR = i + 1;
    int i0 = i > 0 ? i - 1 : 0, i1 = i < nc - 1 ? i + 1 : nc - 1;
    float tx = cl[2 * i1] - cl[2 * i0], ty = cl[2 * i1 + 1] - cl[2 * i0 + 1], tl = hypotf(tx, ty);
    tx /= tl; ty /= tl;
    tg[2 * i] = tx; tg[2 * i + 1] = ty;
    hw = w / 2 * (1 - STROKECUT_SNAKETAPER * acc / total);
    lx[2 * i] = cl[2 * i] - ty * hw; lx[2 * i + 1] = cl[2 * i + 1] + tx * hw;
    rx[2 * i] = cl[2 * i] + ty * hw; rx[2 * i + 1] = cl[2 * i + 1] - tx * hw;
    ex = tx; ey = ty;
  }
  // Leaving the edge at a slant would leave b a sharp wedge beside the neck
  if(iR >= nc - 2 || tg[2 * iR] * nx + tg[2 * iR + 1] * ny < cosf(STROKECUT_SNAKESQUARE)) { free(det); return 1; }
  // Which side starts at u
  int lu = hypotf(lx[0] - ux, lx[1] - uy) < hypotf(rx[0] - ux, rx[1] - uy);
  float * su = lu ? lx : rx, * sv = lu ? rx : lx;
  int d = 0;
  // Neck, u side: a cubic from u leaving along the edge to the finger's side one fillet
  // radius in, arriving along the finger (v side the same, mirrored, on the way back)
  float eux = 0, euy = 0, evx = 0, evy = 0;   // The edge's heading at u and v (u toward v)
  if(neck > 0) {
    float l;
    eux = cx[j0 + 1] - cx[j0]; euy = cy[j0 + 1] - cy[j0]; l = hypotf(eux, euy); eux /= l; euy /= l;
    evx = cx[j1] - cx[j1 - 1]; evy = cy[j1] - cy[j1 - 1]; l = hypotf(evx, evy); evx /= l; evy /= l;
    float k = neck * 0.55f, sx = su[2 * iR], sy = su[2 * iR + 1];
    float c1x = ux + eux * k, c1y = uy + euy * k, c2x = sx - tg[2 * iR] * k, c2y = sy - tg[2 * iR + 1] * k;
    for(int i = 1; i <= nf; i++) {
      float t = (float)i / nf, u = 1 - t;
      det[2 * d] = u * u * u * ux + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * sx;
      det[2 * d + 1] = u * u * u * uy + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * sy;
      d++;
    }
  }
  for(int i = (neck > 0 ? iR : 0) + 1; i < nc; i++) { det[2 * d] = su[2 * i]; det[2 * d + 1] = su[2 * i + 1]; d++; }
  // Tip: half circle from the u side round to the v side, ahead of the end
  float tcx = cl[2 * (nc - 1)], tcy = cl[2 * (nc - 1) + 1];
  float a0 = atan2f(su[2 * (nc - 1) + 1] - tcy, su[2 * (nc - 1)] - tcx);
  float turn = (ex * (su[2 * (nc - 1) + 1] - tcy) - ey * (su[2 * (nc - 1)] - tcx)) > 0 ? -1.0f : 1.0f;
  for(int i = 1; i < capn; i++) {
    float an = a0 + turn * (float)M_PI * i / capn;
    det[2 * d] = tcx + cosf(an) * hw; det[2 * d + 1] = tcy + sinf(an) * hw; d++;
  }
  for(int i = nc - 1; i > (neck > 0 ? iR : 0); i--) { det[2 * d] = sv[2 * i]; det[2 * d + 1] = sv[2 * i + 1]; d++; }
  if(neck > 0) {
    float k = neck * 0.55f, sx = sv[2 * iR], sy = sv[2 * iR + 1];
    float c1x = sx - tg[2 * iR] * k, c1y = sy - tg[2 * iR + 1] * k, c2x = vx - evx * k, c2y = vy - evy * k;
    for(int i = 0; i < nf; i++) {
      float t = (float)i / nf, u = 1 - t;
      det[2 * d] = u * u * u * sx + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * vx;
      det[2 * d + 1] = u * u * u * sy + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * vy;
      d++;
    }
  }
  // New lists (as ids; -1 = u, -2 = v, -3 - i = detour point i):
  //   a: chain[0 .. j0], u, detour, v, chain[j1 .. k], the rest of a
  //   b: chain[k .. j1], v, detour backwards, u, chain[j0 .. 0], the rest of b
  // (whole edge: u, v are chain[0], chain[k] and j0 = -1, j1 = k + 1)
  uint32_t ra = A->n - k - 1, rb = B->n - k - 1;   // Rest of a / b (beyond the chain)
  uint32_t kept = (uint32_t)(j0 + 1) + (k - (uint32_t)j1 + 1);
  uint32_t nla = kept + ra + 2 + d, nlb = kept + rb + 2 + d;
  int64_t * la = malloc(sizeof(int64_t) * nla), * lb = malloc(sizeof(int64_t) * nlb);
  float * pa = malloc(sizeof(float) * 2 * nla), * pb = malloc(sizeof(float) * 2 * nlb);
  if(!la || !lb || !pa || !pb) { free(det); free(la); free(lb); free(pa); free(pb); return -1; }
  uint32_t ka = 0, kb = 0;
  for(int32_t i = 0; i <= j0; i++) la[ka++] = A->id[(ia + i) % A->n];
  la[ka++] = -1;
  for(int i = 0; i < d; i++) la[ka++] = -3 - i;
  la[ka++] = -2;
  for(uint32_t i = j1; i <= k; i++) la[ka++] = A->id[(ia + i) % A->n];
  for(uint32_t i = 0; i < ra; i++) la[ka++] = A->id[(ia + k + 1 + i) % A->n];
  for(uint32_t i = 0; i <= k - (uint32_t)j1 && j1 <= (int32_t)k; i++) lb[kb++] = B->id[(ib + i) % B->n];
  lb[kb++] = -2;
  for(int i = d - 1; i >= 0; i--) lb[kb++] = -3 - i;
  lb[kb++] = -1;
  for(int32_t i = j0; i >= 0; i--) lb[kb++] = B->id[(ib + k - i) % B->n];
  for(uint32_t i = 0; i < rb; i++) lb[kb++] = B->id[(ib + k + 1 + i) % B->n];
  // Whole edge: u and v are existing points (no new base points)
  int64_t uid = whole ? (int64_t)A->id[ia] : -1, vid = whole ? (int64_t)A->id[(ia + k) % A->n] : -2;
  for(int pass = 0; pass < 2; pass++) {
    int64_t * l = pass ? lb : la;
    float * pp = pass ? pb : pa;
    uint32_t n = pass ? kb : ka;
    for(uint32_t i = 0; i < n; i++) {
      int64_t id = l[i];
      if(id == -1) { if(whole) id = uid; else { pp[2 * i] = ux; pp[2 * i + 1] = uy; continue; } }
      if(id == -2) { if(whole) id = vid; else { pp[2 * i] = vx; pp[2 * i + 1] = vy; continue; } }
      if(id <= -3) { pp[2 * i] = det[2 * (-3 - id)]; pp[2 * i + 1] = det[2 * (-3 - id) + 1]; continue; }
      sc_pt(s, id, pp + 2 * i, pp + 2 * i + 1);
    }
  }
  float areab = fabsf(geom_area(pb, kb, NULL, NULL));
  int ok = edgegen_shape_ok(pa, ka, 0.001f) && edgegen_shape_ok(pb, kb, 0.001f) &&
      areab >= STROKECUT_SNAKEKEEP * B->area &&
      cutgraph_thick_ok(pa, ka, STROKECUT_SNAKECLEAR) && cutgraph_thick_ok(pb, kb, STROKECUT_SNAKECLEAR);
#ifdef STROKECUT_DEBUG
  if(!edgegen_shape_ok(pa, ka, 0.001f) || !edgegen_shape_ok(pb, kb, 0.001f)) sc_dbg[1]++;
  else if(areab < STROKECUT_SNAKEKEEP * B->area) sc_dbg[2]++;
  else if(!cutgraph_thick_ok(pa, ka, STROKECUT_SNAKECLEAR)) sc_dbg[3]++;
  else if(!cutgraph_thick_ok(pb, kb, STROKECUT_SNAKECLEAR)) sc_dbg[4]++;
  else sc_dbg[5]++;
#endif
  free(pa);
  free(pb);
  int err = 0;
  if(ok) {
    // Apply: new points, then the lists
    int64_t first = s->npt;
    for(int i = 0; i < d && !err; i++) {
      int64_t id = sc_addpt(s, det[2 * i], det[2 * i + 1]);
      if(id < 0) err = 1;
      else s->snakept[id] = 1;
    }
    if(!whole && !err) {
      uid = sc_addpt(s, ux, uy);
      vid = sc_addpt(s, vx, vy);
      if(uid < 0 || vid < 0) err = 1;
    }
    ScReg nA = {0}, nB = {0};
    for(int pass = 0; pass < 2 && !err; pass++) {
      int64_t * l = pass ? lb : la;
      uint32_t n = pass ? kb : ka;
      ScReg * R = pass ? &nB : &nA;
      for(uint32_t i = 0; i < n && !err; i++) {
        int64_t id = l[i] == -1 ? uid : l[i] == -2 ? vid : l[i] <= -3 ? first + (-3 - l[i]) : l[i];
        err |= sc_push(R, (uint32_t)id);
      }
    }
    if(err) {
      free(nA.id);
      free(nB.id);
    } else {
      A = s->reg + a;
      B = s->reg + b;
      free(A->id);
      A->id = nA.id; A->n = nA.n; A->cap = nA.cap;
      free(B->id);
      B->id = nB.id; B->n = nB.n; B->cap = nB.cap;
      sc_setarea(s, a);
      sc_setarea(s, b);
    }
  }
  free(det);
  free(la);
  free(lb);
  if(err) return -1;
  return ok ? 0 : 1;
}

// Snake about STROKECUT_SNAKES of the edges between a piece and a roomier neighbour
// (each piece sends and takes at most STROKECUT_SNAKEOUT / STROKECUT_SNAKEIN). Returns 0, or -1 out of memory.
static int sc_snakes(Sc * s, float sc) {
  ScSnake * cand = NULL;
  uint32_t nc = 0, capc = 0;
  for(uint32_t a = 0; a < s->nreg; a++) {
    const ScReg * A = s->reg + a;
    if(A->whimsy) continue;
    uint32_t j0 = A->n;
    for(uint32_t i = 0; i < A->n; i++) if(sc_junction(s, A->id[i])) { j0 = i; break; }
    if(j0 == A->n) continue;
    uint32_t i = j0;
    do {
      uint32_t k = 1;
      float L = 0;
      while(1) {
        float x0, y0, x1, y1;
        sc_pt(s, A->id[(i + k - 1) % A->n], &x0, &y0);
        sc_pt(s, A->id[(i + k) % A->n], &x1, &y1);
        L += hypotf(x1 - x0, y1 - y0);
        if(sc_junction(s, A->id[(i + k) % A->n])) break;
        k++;
      }
      if(L >= STROKECUT_SNAKEMIN * sc) {
        if(nc == capc) {
          capc = capc ? capc * 2 : 64;
          ScSnake * nw = realloc(cand, sizeof(ScSnake) * capc);
          if(!nw) { free(cand); return -1; }
          cand = nw;
        }
        cand[nc++] = (ScSnake) { a, A->id[i], A->id[(i + k) % A->n] };
      }
      i = (i + k) % A->n;
    } while(i != j0);
  }
  // Snakes each piece has sent and taken
  uint8_t * out = calloc(s->nreg ? s->nreg : 1, 1), * in = calloc(s->nreg ? s->nreg : 1, 1);
  if(!out || !in) { free(cand); free(out); free(in); return -1; }
  // Random order, so it isn't always the first pieces that get them
  for(uint32_t i = nc; i > 1; i--) {
    uint32_t j = rng_below(&s->r, i);
    ScSnake t = cand[i - 1]; cand[i - 1] = cand[j]; cand[j] = t;
  }
  int err = 0;
  for(uint32_t c = 0; c < nc && !err; c++) {
    ScSnake * sn = cand + c;
    if(out[sn->a] >= STROKECUT_SNAKEOUT || rng_range(&s->r, 0, 1) >= STROKECUT_SNAKES) continue;
    const ScReg * A = s->reg + sn->a;
    // Find the edge again: from u along a's list to the next junction, which must be v,
    // with no snake on it yet
    uint32_t ia = A->n, k = 0;
    for(uint32_t i = 0; i < A->n && ia == A->n; i++) {
      if(A->id[i] != sn->u) continue;
      uint32_t j = 1, snaked = 0;
      while(j < A->n && !sc_junction(s, A->id[(i + j) % A->n])) snaked |= s->snakept[A->id[(i + j) % A->n]], j++;
      if(A->id[(i + j) % A->n] == sn->v && !snaked) { ia = i; k = j; }
    }
    if(ia == A->n || k + 1 > STROKECUT_SNAKECHAIN) continue;
    uint32_t ib;
    int64_t b = sc_across(s, sn->a, A->id[ia], A->id[(ia + 1) % A->n], &ib);
    if(b < 0 || in[b] >= STROKECUT_SNAKEIN || s->reg[b].whimsy || s->reg[b].area < STROKECUT_SNAKEROOM * A->area) continue;
#ifdef STROKECUT_DEBUG
    sc_dbg[7]++;
#endif
    // ib is the far side's step (x1 -> u); the chain there runs from v back to u
    ib = (ib + s->reg[b].n - (k - 1)) % s->reg[b].n;
    for(int t = 0; t < STROKECUT_SNAKETRIES; t++) {
      int res = sc_snake(s, sn->a, ia, k, b, ib, sc);
      if(res < 0) err = 1;
      if(res <= 0) {
        out[sn->a]++;
        in[b]++;
        break;
      }
    }
  }
#ifdef STROKECUT_DEBUG
  sc_dbg[6] += nc;
  fprintf(stderr, "snakes: candidates %ld eligible %ld | too close %ld crossing %ld area %ld thin-a %ld thin-b %ld ok %ld\n",
      sc_dbg[6], sc_dbg[7], sc_dbg[0], sc_dbg[1], sc_dbg[2], sc_dbg[3], sc_dbg[4], sc_dbg[5]);
#endif
  free(out);
  free(in);
  free(cand);
  sc_recount(s);
  return err ? -1 : 0;
}

// ---- Graph ----

typedef struct {
  uint32_t lo, hi, key;   // Junction ids (lo < hi) and the interior point next to lo (or ~0)
  uint32_t reg;           // Region walking it
  uint32_t from;          // Junction it starts at
  uint32_t start, len;    // Interior points: chainpts[start .. start + len) in walking order
} ScHalf;

static int sc_halfcmp(const void * a, const void * b) {
  const ScHalf * x = a, * y = b;
  if(x->lo != y->lo) return x->lo < y->lo ? -1 : 1;
  if(x->hi != y->hi) return x->hi < y->hi ? -1 : 1;
  if(x->key != y->key) return x->key < y->key ? -1 : 1;
  return (x->reg > y->reg) - (x->reg < y->reg);
}

static int sc_onborder(const Sc * s, uint32_t id) {
  float x = s->xy[2 * id], y = s->xy[2 * id + 1];
  return x <= 1e-5f || y <= 1e-5f || x >= s->w - 1e-5f || y >= s->h - 1e-5f;
}

static int sc_tograph(Sc * s, CutGraph * g, uint32_t count) {
  uint32_t nreg = s->nreg, total = 0;
  for(uint32_t r = 0; r < nreg; r++) total += s->reg[r].n;
  // Junctions: points on 3+ boundaries (the puzzle border counts), and the puzzle corners
  int32_t * vmap = malloc(sizeof(int32_t) * s->npt);
  int32_t * piece = malloc(sizeof(int32_t) * nreg);
  ScHalf * half = malloc(sizeof(ScHalf) * (total ? total : 1));
  uint32_t * chainpts = malloc(sizeof(uint32_t) * (total ? total : 1));
  int err = !vmap || !piece || !half || !chainpts;
  uint32_t nv = 0, nh = 0, nc = 0;
  if(!err) {
    for(uint32_t i = 0; i < s->npt; i++) {
      float x = s->xy[2 * i], y = s->xy[2 * i + 1];
      int corner = (x <= 1e-5f || x >= s->w - 1e-5f) && (y <= 1e-5f || y >= s->h - 1e-5f);
      int deg = s->occ[i] + sc_onborder(s, i);
      vmap[i] = s->occ[i] && (deg >= 3 || corner) ? (int32_t)nv++ : -1;
    }
    uint32_t np = 0, nw = 0;
    for(uint32_t r = 0; r < nreg; r++) if(!s->reg[r].whimsy) piece[r] = np++;
    for(uint32_t r = 0; r < nreg; r++) if(s->reg[r].whimsy) piece[r] = count + nw++;
    if(np != count) err = 1;
  }
  // Chains between junctions, per region
  for(uint32_t r = 0; r < nreg && !err; r++) {
    const ScReg * R = s->reg + r;
    uint32_t j0 = R->n;
    for(uint32_t i = 0; i < R->n; i++) if(vmap[R->id[i]] >= 0) { j0 = i; break; }
    if(j0 == R->n) { err = 1; break; }
    uint32_t i = j0;
    do {
      uint32_t u = R->id[i], start = nc, k = (i + 1) % R->n;
      while(vmap[R->id[k]] < 0) { chainpts[nc++] = R->id[k]; k = (k + 1) % R->n; }
      uint32_t v = R->id[k], len = nc - start;
      ScHalf * hf = half + nh++;
      hf->reg = r;
      hf->from = u;
      hf->start = start;
      hf->len = len;
      hf->lo = u < v ? u : v;
      hf->hi = u < v ? v : u;
      hf->key = len == 0 ? 0xFFFFFFFFu : u < v ? chainpts[start] : chainpts[start + len - 1];
      if(u == v) err = 1;
      i = k;
    } while(i != j0 && !err);
  }
  if(!err) {
    qsort(half, nh, sizeof(ScHalf), sc_halfcmp);
    memset(g, 0, sizeof(CutGraph));
    g->count = count + (s->nreg - count);
    g->width = s->w;
    g->height = s->h;
    g->v = malloc(sizeof(CutVert) * (nv ? nv : 1));
    g->e = calloc(nh ? nh : 1, sizeof(CutEdge));
    g->pts = malloc(sizeof(float) * 2 * (nc ? nc : 1));
    err = !g->v || !g->e || !g->pts;
  }
  if(!err) {
    g->nv = nv;
    for(uint32_t i = 0; i < s->npt; i++) {
      if(vmap[i] >= 0) { g->v[vmap[i]].x = s->xy[2 * i]; g->v[vmap[i]].y = s->xy[2 * i + 1]; }
    }
    for(uint32_t i = 0; i < nh && !err; ) {
      ScHalf * h0 = half + i;
      int pair = i + 1 < nh && h0->lo == half[i + 1].lo && h0->hi == half[i + 1].hi && h0->key == half[i + 1].key;
      CutEdge * e = g->e + g->ne++;
      e->a = vmap[h0->lo];
      e->b = vmap[h0->hi];
      e->left = e->right = -1;
      // a < b by vertex number; walk direction relative to that
      int swap = e->a > e->b;
      if(swap) { uint32_t t = e->a; e->a = e->b; e->b = t; }
      for(uint32_t k = 0; k < (uint32_t)(pair ? 2 : 1); k++) {
        ScHalf * hk = half + i + k;
        int fwd = (vmap[hk->from] == (int32_t)e->a);
        if(fwd) e->left = piece[hk->reg];
        else e->right = piece[hk->reg];
        if(k == 0) {
          e->p0 = g->npts;
          e->npts = hk->len;
          for(uint32_t q = 0; q < hk->len; q++) {
            uint32_t id = chainpts[hk->start + (fwd ? q : hk->len - 1 - q)];
            g->pts[2 * g->npts] = s->xy[2 * id];
            g->pts[2 * g->npts + 1] = s->xy[2 * id + 1];
            g->npts++;
          }
        }
        if(s->reg[hk->reg].whimsy) e->flags |= CUTEDGE_WHIMSY;
        for(uint32_t q = 0; q < hk->len; q++) if(s->snakept[chainpts[hk->start + q]]) e->flags |= CUTEDGE_SNAKE;
      }
      if(!pair) {
        // Unpaired: must be on the puzzle border
        if(!sc_onborder(s, h0->lo) || !sc_onborder(s, h0->hi)) err = 1;
        for(uint32_t q = 0; q < h0->len && !err; q++) if(!sc_onborder(s, chainpts[h0->start + q])) err = 1;
      }
      i += pair ? 2 : 1;
    }
  }
  if(!err && cutgraph_rewalk(g)) err = 1;
  free(vmap);
  free(piece);
  free(half);
  free(chainpts);
  if(err) cutgraph_free(g);
  return err;
}

// ---- Whole cut ----

static void sc_free(Sc * s) {
  for(uint32_t r = 0; r < s->nreg; r++) free(s->reg[r].id);
  free(s->reg);
  free(s->xy);
  free(s->occ);
  free(s->snakept);
  free(s->hole);
  free(s->buf);
}

static int sc_build(CutGraph * g, float w, float h, uint32_t count, uint8_t whimsy, uint8_t set, uint32_t seed) {
  Sc s = {0};
  s.w = w;
  s.h = h;
  rng_init(&s.r, seed);
  int err = 0;
  int64_t r0 = sc_newreg(&s, 0);
  float corners[8] = { 0, 0, w, 0, w, h, 0, h };
  if(r0 < 0) err = -1;
  for(int i = 0; i < 4 && !err; i++) {
    int64_t id = sc_addpt(&s, corners[2 * i], corners[2 * i + 1]);
    if(id < 0 || sc_push(s.reg + r0, id)) err = -1;
  }
  if(!err) {
    sc_setarea(&s, r0);
    sc_recount(&s);
    int placed = sc_placewhimsies(&s, whimsy_count(whimsy, count), set);
    if(placed < 0) err = -1;
    // Anchor every whimsy (the host region must lose its slit before the next), nearest the
    // border first: later ones then find strokes close by to end on
    while(!err && s.nhole) {
      uint32_t pick = 0;
      float pickd = 1e30f;
      for(uint32_t k = 0; k < s.nhole; k++) {
        const ScReg * W = s.reg + s.hole[k];
        float d = 1e30f;
        for(uint32_t i = 0; i < W->n; i++) {
          float x = s.xy[2 * W->id[i]], y = s.xy[2 * W->id[i] + 1];
          d = fminf(d, fminf(fminf(x, s.w - x), fminf(y, s.h - y)));
        }
        if(d < pickd) { pickd = d; pick = k; }
      }
      uint32_t tmp = s.hole[pick];
      s.hole[pick] = s.hole[s.nhole - 1];
      s.hole[s.nhole - 1] = tmp;
      int res = sc_anchor(&s, s.nhole - 1);
      if(res) err = res;
#ifdef STROKECUT_DEBUG
      if(res) fprintf(stderr, "anchor failed, %u holes left of %d\n", s.nhole + 1, placed);
#endif
    }
  }
  int snaked = 0;
  while(!err) {
    uint32_t pieces = 0;
    int64_t best = -1;
    for(uint32_t r = 0; r < s.nreg; r++) {
      if(s.reg[r].whimsy) continue;
      pieces++;
      if(!s.reg[r].stuck && (best < 0 || s.reg[r].area > s.reg[best].area)) best = r;
    }
    if(pieces >= count) break;
    // Snakes go in part way, while pieces are still big enough for them: the cuts after
    // that end on them, so the snaking lines stay
    if(!snaked && pieces >= (uint32_t)(count * STROKECUT_SNAKEAT)) {
      snaked = 1;
      if(sc_snakes(&s, sqrtf(w * h / pieces))) { err = -1; break; }
      continue;
    }
    if(best < 0) {
#ifdef STROKECUT_DEBUG
      fprintf(stderr, "all stuck at %u pieces of %u\n", pieces, count);
#endif
      err = 1;
      break;
    }
    int res = sc_split(&s, best);
    if(res < 0) err = -1;
    else if(res > 0) s.reg[best].stuck = 1;
  }
  if(!err) err = sc_tograph(&s, g, count);
  sc_free(&s);
  return err;
}

int strokecut_graph(CutGraph * g, float width, float height, uint32_t count, uint8_t whimsy, uint8_t set,
    uint32_t seed) {
  memset(g, 0, sizeof(CutGraph));
  if(count < 2 || count > 0xFFF0 || !(width > 0) || !(height > 0)) return 1;
  for(uint32_t t = 0; t < 8; t++) {
    int res = sc_build(g, width, height, count, whimsy, set, seed ^ (t * 0x85EBCA6Bu));
    if(res == 0) {
      g->tries = t + 1;
      return 0;
    }
    if(res < 0) return 1;
  }
  return 1;
}

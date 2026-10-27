#include "whimsy.h"
#include "rng.h"
#include "geom.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WH_PI 3.14159265f
#define WH_HOSTAREA 0.3f      // Smallest piece left after cutting a whimsy out (cells^2)
#define WH_MINPART 0.03f      // Shortest edge piece left where a whimsy crosses (cells)
#define WH_CLEARANCE 0.06f    // Body clearance for pieces around a whimsy (cells)
#define WH_GRIDNEAR 0.45f     // Grid edges this close to a whimsy get new tabs
#define WH_HOSTTAB 0.5f       // A piece next to a whimsy needs a straight inner edge this long
                              // (WHIMSY_TABEDGE if that edge ends at a whimsy)
#define WH_MINTHICK 0.25f     // ...and must be this thick (biggest circle inside, cells)
// Heart proportions (any units; normalized after), approximating a cute heart picture the
// user picked (its lobes are slightly oval): a fairly deep pointed dip, sharp point
#define WH_HEART_A 1.9f       // Lobe circle centers at x = +-A
#define WH_HEART_R 2.4f       // Lobe radius
#define WH_HEART_TIPY 4.6f    // Tip circle center (lobe centers at y = 0)
#define WH_HEART_TIPR 0.001f  // Tip roundness (sharp)

// ---- Shapes ----

// One round of Chaikin corner cutting on a closed outline (n points -> 2n)
static uint32_t whimsy_chaikin(const float * in, uint32_t n, float * out) {
  for(uint32_t i = 0; i < n; i++) {
    const float * a = in + 2 * i, * b = in + 2 * ((i + 1) % n);
    out[4 * i] = 0.75f * a[0] + 0.25f * b[0];
    out[4 * i + 1] = 0.75f * a[1] + 0.25f * b[1];
    out[4 * i + 2] = 0.25f * a[0] + 0.75f * b[0];
    out[4 * i + 3] = 0.25f * a[1] + 0.75f * b[1];
  }
  return 2 * n;
}

// Scale to area 1, move the centroid to 0, make it clockwise (positive area, y down)
static void whimsy_normalize(float * xy, uint32_t n) {
  float cx = 0, cy = 0;
  float a = geom_area(xy, n, &cx, &cy);
  if(a < 0) {
    for(uint32_t i = 0; i < n / 2; i++) {
      float * p = xy + 2 * i, * q = xy + 2 * (n - 1 - i);
      float t0 = p[0], t1 = p[1];
      p[0] = q[0]; p[1] = q[1];
      q[0] = t0; q[1] = t1;
    }
    a = -a;
  }
  float s = 1 / sqrtf(a);
  for(uint32_t i = 0; i < n; i++) {
    xy[2 * i] = (xy[2 * i] - cx) * s;
    xy[2 * i + 1] = (xy[2 * i + 1] - cy) * s;
  }
}

// Ray from the origin at angle t: distance to the far side of circle (cx, cy, r), or -1
static float whimsy_ray(float t, float cx, float cy, float r) {
  float ux = cosf(t), uy = sinf(t);
  float b = cx * ux + cy * uy, c = cx * cx + cy * cy - r * r, d = b * b - c;
  return d < 0 ? -1 : b + sqrtf(d);
}

// Outline of a union of circles that's star-shaped around the origin, sampled at m angles
// (sharp where circles meet). petals > 0: that many circles of radius r at distance d
// around a middle circle; petals = 0: a cloud.
static uint32_t whimsy_circles(float * xy, uint32_t m, uint32_t petals, float d, float r) {
  
  static const float cloud[][3] = {
    { 0, 0, 0.6f }, { -0.4f, -0.34f, 0.44f }, { 0.4f, -0.34f, 0.44f },
    { -0.85f, 0.04f, 0.4f }, { 0.85f, 0.04f, 0.4f },
    { -0.36f, 0.34f, 0.46f }, { 0.36f, 0.34f, 0.46f },
  };
  for(uint32_t i = 0; i < m; i++) {
    float t = 2 * WH_PI * i / m, best = 0;
    if(petals) {
      best = whimsy_ray(t, 0, 0, d * 0.7f);
      for(uint32_t k = 0; k < petals; k++) {
        float a = 2 * WH_PI * k / petals - WH_PI / 2;
        best = fmaxf(best, whimsy_ray(t, d * cosf(a), d * sinf(a), r));
      }
    } else {
      for(uint32_t k = 0; k < sizeof(cloud) / sizeof(cloud[0]); k++) best = fmaxf(best, whimsy_ray(t, cloud[k][0], cloud[k][1], cloud[k][2]));
    }
    xy[2 * i] = best * cosf(t);
    xy[2 * i + 1] = best * sinf(t);
  }
  return m;
}

// Append points on circle (cx, cy, r) from angle a0 to a1 (y down: increasing angle runs
// clockwise on screen), excluding a1
static uint32_t whimsy_arc(float * xy, uint32_t n, float cx, float cy, float r, float a0, float a1, uint32_t steps) {
  for(uint32_t i = 0; i < steps; i++) {
    float a = a0 + (a1 - a0) * i / steps;
    xy[2 * n] = cx + r * cosf(a);
    xy[2 * n + 1] = cy + r * sinf(a);
    n++;
  }
  return n;
}

// Outward normal of the outer tangent line on the side given by sign (+1 right, -1 left)
// between circle (x1, y1, r1) and circle (x2, y2, r2)
static void whimsy_tangent(float x1, float y1, float r1, float x2, float y2, float r2, float sign, float * nx, float * ny) {
  float dx = x2 - x1, dy = y2 - y1, l = sqrtf(dx * dx + dy * dy);
  dx /= l; dy /= l;
  float c = (r1 - r2) / l, s = sqrtf(1 - c * c);
  // Rotate d by +-acos(c); pick the one on the asked side
  float ax = dx * c - dy * s, ay = dx * s + dy * c, bx = dx * c + dy * s, by = -dx * s + dy * c;
  if((ax > bx) == (sign > 0)) { *nx = ax; *ny = ay; } else { *nx = bx; *ny = by; }
}

// Heart in the proportions of a classic cute heart: lobe circles (+-a, 0, r) whose overlap
// makes a shallow pointed dip, a small tip circle, straight tangent sides. Clockwise.
static uint32_t whimsy_heart(float * xy) {
  const float a = WH_HEART_A, r = WH_HEART_R, tr = WH_HEART_TIPR, ty = WH_HEART_TIPY;
  float dipy = -sqrtf(r * r - a * a);
  float rx, ry, lx, ly;
  whimsy_tangent(a, 0, r, 0, ty, tr, 1, &rx, &ry);
  whimsy_tangent(-a, 0, r, 0, ty, tr, -1, &lx, &ly);
  float ar = atan2f(ry, rx), al = atan2f(ly, lx);
  uint32_t n = 0;
  // Right lobe: from the dip over the top and down to the right tangent point
  float a0 = atan2f(dipy, -a);
  n = whimsy_arc(xy, n, a, 0, r, a0, ar, 36);
  // Down the right side to the tip, round the tip, up the left side
  n = whimsy_arc(xy, n, 0, ty, tr, ar, al, 8);
  // Left lobe: from the left tangent point round to the dip
  float a1 = atan2f(dipy, a);
  while(a1 < al) a1 += 2 * WH_PI;
  n = whimsy_arc(xy, n, -a, 0, r, al, a1, 36);
  return n;
}

uint32_t whimsy_setsize(uint8_t set) {
  return 4;
}

uint32_t whimsy_shape(uint8_t set, uint32_t shape, float * xy) {
  float tmp[WHIMSY_MAXSHAPE * 2];
  uint32_t n = 0;
  switch(shape % 4) {
    case 0:     // Heart: two round lobes, straight sides, sharp point and dip
      n = whimsy_heart(xy);
      break;
    case 1: {   // Five point star, tips and valleys rounded
      float a[20 * 2];
      for(uint32_t i = 0; i < 10; i++) {
        float t = -WH_PI / 2 + WH_PI * i / 5, r = (i & 1) ? 0.5f : 1;
        tmp[2 * i] = r * cosf(t);
        tmp[2 * i + 1] = r * sinf(t);
      }
      whimsy_chaikin(tmp, 10, a);
      n = whimsy_chaikin(a, 20, xy);
      break;
    }
    case 2:     // Six fat petals around a middle
      n = whimsy_circles(xy, 120, 6, 0.66f, 0.34f);
      break;
    default:    // Cloud: round bumps on top of a flat-ish base
      n = whimsy_circles(xy, 120, 0, 0, 0);
      break;
  }
  whimsy_normalize(xy, n);
  return n;
}

uint32_t whimsy_count(uint8_t level, uint32_t count) {
  if(level == WHIMSY_FEW) return count / 60 > 1 ? (count + 30) / 60 : 1;
  if(level == WHIMSY_MANY) return count / 25 > 2 ? (count + 12) / 25 : 2;
  return 0;
}

// ---- Geometry helpers ----

// Distance from segment ab to the polygon's outline (0 if they cross)
static float whimsy_segdist(const float * xy, uint32_t n, float ax, float ay, float bx, float by) {
  float best = 1e30f;
  for(uint32_t j = 0; j < n; j++) {
    const float * p = xy + 2 * j, * q = xy + 2 * ((j + 1) % n);
    float rx = bx - ax, ry = by - ay, sx = q[0] - p[0], sy = q[1] - p[1];
    float den = rx * sy - ry * sx;
    if(den != 0) {
      float t = ((p[0] - ax) * sy - (p[1] - ay) * sx) / den;
      float u = ((p[0] - ax) * ry - (p[1] - ay) * rx) / den;
      if(t >= 0 && t <= 1 && u >= 0 && u <= 1) return 0;
    }
    float d = fminf(fminf(geom_pointseg(ax, ay, p[0], p[1], q[0], q[1]), geom_pointseg(bx, by, p[0], p[1], q[0], q[1])),
        fminf(geom_pointseg(p[0], p[1], ax, ay, bx, by), geom_pointseg(q[0], q[1], ax, ay, bx, by)));
    if(d < best) best = d;
  }
  return best;
}

// ---- Checking the pieces around a whimsy ----

void whimsy_measure(const float * xy, uint32_t n, float * thick, float * spike) {
  float x0 = xy[0], y0 = xy[1], x1 = xy[0], y1 = xy[1];
  for(uint32_t i = 1; i < n; i++) {
    x0 = fminf(x0, xy[2 * i]); x1 = fmaxf(x1, xy[2 * i]);
    y0 = fminf(y0, xy[2 * i + 1]); y1 = fmaxf(y1, xy[2 * i + 1]);
  }
  const float st = WHIMSY_MEASURESTEP, r = WHIMSY_SPIKER;
  int32_t gw = (int32_t)((x1 - x0) / st) + 1, gh = (int32_t)((y1 - y0) / st) + 1;
  // Distance to the outline at every grid point inside (-1 = outside)
  float * d = malloc(sizeof(float) * gw * gh);
  uint8_t * reach = calloc(gw * gh, 1);
  *thick = 0;
  *spike = 0;
  if(!d || !reach) {
    free(d);
    free(reach);
    return;
  }
  for(int32_t gy = 0; gy < gh; gy++) {
    for(int32_t gx = 0; gx < gw; gx++) {
      float x = x0 + (gx + 0.5f) * st, y = y0 + (gy + 0.5f) * st;
      float * dp = d + gy * gw + gx;
      *dp = -1;
      if(!geom_inside(xy, n, x, y)) continue;
      float dmin = 1e30f;
      for(uint32_t i = 0, j = n - 1; i < n; j = i++) {
        float e = geom_pointseg(x, y, xy[2 * j], xy[2 * j + 1], xy[2 * i], xy[2 * i + 1]);
        if(e < dmin) dmin = e;
      }
      *dp = dmin;
      if(dmin > *thick) *thick = dmin;
    }
  }
  // A disc of radius r that fits (center at least r from the outline) reaches everything
  // within r of its center; inside points no such disc reaches are in a part thinner than 2r
  // (Grid points only approximate where a disc can sit: reach a grid step further, or a
  // band along every edge would count as lost)
  float reachr = r + st;
  int32_t rg = (int32_t)ceilf(reachr / st);
  for(int32_t gy = 0; gy < gh; gy++) {
    for(int32_t gx = 0; gx < gw; gx++) {
      if(d[gy * gw + gx] < r) continue;
      for(int32_t ky = -rg; ky <= rg; ky++) {
        for(int32_t kx = -rg; kx <= rg; kx++) {
          int32_t x = gx + kx, y = gy + ky;
          if(x < 0 || y < 0 || x >= gw || y >= gh || (kx * kx + ky * ky) * st * st > reachr * reachr) continue;
          reach[y * gw + x] = 1;
        }
      }
    }
  }
  uint32_t lost = 0;
  for(int32_t i = 0; i < gw * gh; i++) lost += d[i] >= 0 && !reach[i];
  *spike = lost * st * st;
  free(d);
  free(reach);
}

// ---- Inserting a shape into the graph ----

typedef struct {
  uint32_t edge;    // Original edge it's on
  float t;          // Along that edge (0..1)
  float s;          // Along the shape (segment index + fraction)
  uint32_t v;       // New vertex
} WhCross;

// Work for one insertion: the shape, where it crosses the cut, what it covers
typedef struct {
  const float * xy;
  uint32_t n;
  WhCross * cross;
  uint32_t ncross, capcross;
  uint8_t * inside;   // Per vertex: inside the shape
  uint8_t * dead;     // Per edge: wholly inside the shape
} WhInsert;

static int whimsy_crosscmp_t(const void * a, const void * b) {
  const WhCross * x = a, * y = b;
  if(x->edge != y->edge) return x->edge < y->edge ? -1 : 1;
  return x->t < y->t ? -1 : x->t > y->t;
}

static int whimsy_crosscmp_s(const void * a, const void * b) {
  const WhCross * x = a, * y = b;
  return x->s < y->s ? -1 : x->s > y->s;
}

// Fill in an edge between vertices u -> v (in that walking direction for piece walker,
// other on the far side), keeping a < b
static void whimsy_edge(CutEdge * e, uint32_t u, uint32_t v, int32_t walker, int32_t other, uint8_t flags) {
  memset(e, 0, sizeof(CutEdge));
  e->flags = flags;
  if(u < v) {
    e->a = u; e->b = v; e->left = walker; e->right = other;
  } else {
    e->a = v; e->b = u; e->left = other; e->right = walker;
  }
}

// Step 1 (changes nothing): every crossing of an edge with the shape, which vertices are
// inside, which edges are wholly inside. Returns 0 if it can go in, 1 if not, -1 out of memory.
static int whimsy_crossings(const CutGraph * g, WhInsert * w) {
  const float * xy = w->xy;
  uint32_t n = w->n;
  float sx0 = xy[0], sy0 = xy[1], sx1 = xy[0], sy1 = xy[1];
  for(uint32_t i = 1; i < n; i++) {
    sx0 = fminf(sx0, xy[2 * i]); sx1 = fmaxf(sx1, xy[2 * i]);
    sy0 = fminf(sy0, xy[2 * i + 1]); sy1 = fmaxf(sy1, xy[2 * i + 1]);
  }
  w->inside = malloc(g->nv ? g->nv : 1);
  w->dead = calloc(g->ne ? g->ne : 1, 1);
  if(!w->inside || !w->dead) return -1;
  for(uint32_t v = 0; v < g->nv; v++) w->inside[v] = geom_inside(xy, n, g->v[v].x, g->v[v].y);
  for(uint32_t i = 0; i < g->ne; i++) {
    const CutEdge * e = g->e + i;
    float ax = g->v[e->a].x, ay = g->v[e->a].y, bx = g->v[e->b].x, by = g->v[e->b].y;
    if(fmaxf(ax, bx) < sx0 || fminf(ax, bx) > sx1 || fmaxf(ay, by) < sy0 || fminf(ay, by) > sy1) {
      if(w->inside[e->a] || w->inside[e->b]) return 1;
      continue;
    }
    uint32_t before = w->ncross;
    for(uint32_t j = 0; j < n; j++) {
      const float * p = xy + 2 * j, * q = xy + 2 * ((j + 1) % n);
      float rx = bx - ax, ry = by - ay, qx = q[0] - p[0], qy = q[1] - p[1];
      float den = rx * qy - ry * qx;
      if(den == 0) continue;
      float t = ((p[0] - ax) * qy - (p[1] - ay) * qx) / den;
      float u = ((p[0] - ax) * ry - (p[1] - ay) * rx) / den;
      if(t < -1e-6f || t > 1 + 1e-6f || u < -1e-6f || u > 1 + 1e-6f) continue;
      // Crossing right at a corner or a shape point is ambiguous; so is crossing the border
      // or another whimsy (placement keeps away from both)
      if(t < 1e-4f || t > 1 - 1e-4f || u < 1e-4f || u > 1 - 1e-4f) return 1;
      if(e->npts || (e->flags & CUTEDGE_WHIMSY) || e->left < 0 || e->right < 0) return 1;
      if(w->ncross == w->capcross) {
        w->capcross = w->capcross ? w->capcross * 2 : 32;
        WhCross * nc = realloc(w->cross, sizeof(WhCross) * w->capcross);
        if(!nc) return -1;
        w->cross = nc;
      }
      w->cross[w->ncross++] = (WhCross) { i, t, j + u, 0 };
    }
    uint32_t ends = w->inside[e->a] != w->inside[e->b];
    if(w->ncross == before) {
      if(ends) return 1;
      if(w->inside[e->a]) w->dead[i] = 1;
    } else if(((w->ncross - before) & 1) != ends) return 1;
  }
  return w->ncross < 2;
}

// Step 2: a new vertex at each crossing, and the parts of crossed edges outside the shape
// (tabs given out later); untouched edges are copied, edges wholly inside dropped. Writes
// the new edge list to ne (room for ne + 2 * ncross). Returns 0, or 1 if a part left by a
// crossing would be too short.
static int whimsy_split(CutGraph * g, WhInsert * w, CutEdge * ne, uint32_t * nn) {
  qsort(w->cross, w->ncross, sizeof(WhCross), whimsy_crosscmp_t);
  for(uint32_t k = 0; k < w->ncross; k++) {
    const CutEdge * e = g->e + w->cross[k].edge;
    float t = w->cross[k].t;
    w->cross[k].v = g->nv + k;
    g->v[g->nv + k] = (CutVert) { g->v[e->a].x + t * (g->v[e->b].x - g->v[e->a].x),
        g->v[e->a].y + t * (g->v[e->b].y - g->v[e->a].y) };
  }
  g->nv += w->ncross;
  *nn = 0;
  for(uint32_t i = 0, k = 0; i < g->ne; i++) {
    const CutEdge * e = g->e + i;
    if(k >= w->ncross || w->cross[k].edge != i) {
      if(!w->dead[i]) ne[(*nn)++] = *e;
      continue;
    }
    // Walk a -> b, keeping the parts outside
    int in = w->inside[e->a];
    uint32_t from = e->a;
    float tfrom = 0, L = cutgraph_edgelen(g, e);
    for(; k < w->ncross && w->cross[k].edge == i; k++) {
      if(!in) {
        if((w->cross[k].t - tfrom) * L < WH_MINPART) return 1;
        whimsy_edge(ne + (*nn)++, from, w->cross[k].v, e->left, e->right, 0);
      }
      in = !in;
      from = w->cross[k].v;
      tfrom = w->cross[k].t;
    }
    if(!in) {
      if((1 - tfrom) * L < WH_MINPART) return 1;
      whimsy_edge(ne + (*nn)++, from, e->b, e->left, e->right, 0);
    }
  }
  return 0;
}

// Step 3: the shape's outline between consecutive crossings, as polyline edges between the
// new piece wid and the piece outside the shape there. The shape runs clockwise, so the
// whimsy walks it forward. Points go to pts from *np. Returns 0, or 1 if an arc's two ends
// don't border the same piece.
static int whimsy_arcs(const CutGraph * g, WhInsert * w, int32_t wid, CutEdge * ne, uint32_t * nn, float * pts, uint32_t * np) {
  const float * xy = w->xy;
  uint32_t n = w->n;
  qsort(w->cross, w->ncross, sizeof(WhCross), whimsy_crosscmp_s);
  for(uint32_t k = 0; k < w->ncross; k++) {
    const WhCross * x = w->cross + k, * y = w->cross + (k + 1) % w->ncross;
    const CutEdge * ex = g->e + x->edge, * ey = g->e + y->edge;
    // The host is on the side of ex the shape heads into; the left piece's inside is
    // toward (-dy, dx) of a -> b
    uint32_t j = (uint32_t)x->s;
    float dx = xy[2 * ((j + 1) % n)] - xy[2 * j], dy = xy[2 * ((j + 1) % n) + 1] - xy[2 * j + 1];
    float ex_x = g->v[ex->b].x - g->v[ex->a].x, ex_y = g->v[ex->b].y - g->v[ex->a].y;
    int32_t host = dx * -ex_y + dy * ex_x > 0 ? ex->left : ex->right;
    if(host < 0 || (ey->left != host && ey->right != host)) return 1;
    // Shape points strictly between the two crossings
    uint32_t last = (uint32_t)y->s;
    uint32_t m = y->s > x->s ? last - j : last + n - j;
    uint32_t p0 = *np;
    for(uint32_t i = 0; i < m; i++) {
      uint32_t idx = (j + 1 + i) % n;
      pts[2 * *np] = xy[2 * idx];
      pts[2 * *np + 1] = xy[2 * idx + 1];
      (*np)++;
    }
    CutEdge * e = ne + (*nn)++;
    whimsy_edge(e, x->v, y->v, wid, host, CUTEDGE_WHIMSY);
    if(e->a != x->v) {
      // Stored b -> a: reverse the points
      for(uint32_t i = 0; i < m / 2; i++) {
        float * p = pts + 2 * (p0 + i), * q = pts + 2 * (p0 + m - 1 - i);
        float t0 = p[0], t1 = p[1];
        p[0] = q[0]; p[1] = q[1];
        q[0] = t0; q[1] = t1;
      }
    }
    e->p0 = p0;
    e->npts = m;
  }
  return 0;
}

// Step 5: the pieces around new whimsy wid must stay solid: enough area, no thin necks or
// spikes, thick enough, and still able to get a tab. Returns 0 if fine, 1 if not, -1 out
// of memory.
static int whimsy_check(const CutGraph * g, int32_t wid) {
  float * body = malloc(sizeof(float) * 2 * cutgraph_maxpoints(g));
  uint8_t * wv = cutgraph_whimsyverts(g);
  CutTabStyle st = { .mintab = WH_HOSTTAB, .whimsytab = WHIMSY_TABEDGE };
  int bad = 0;
  if(!body || !wv) bad = -1;
  for(uint32_t i = 0; i < g->ne && !bad; i++) {
    const CutEdge * e = g->e + i;
    if(!(e->flags & CUTEDGE_WHIMSY) || (e->left != wid && e->right != wid)) continue;
    int32_t sides[2] = { e->left, e->right };
    for(int s = 0; s < 2 && !bad; s++) {
      int32_t p = sides[s];
      uint32_t bn = cutgraph_body(g, p, 1, 0, 0, body);
      if(!edgegen_shape_ok(body, bn, WH_CLEARANCE)) bad = 1;
      if(p == wid || bad) continue;
      if(geom_area(body, bn, NULL, NULL) < WH_HOSTAREA) bad = 1;
      int cantab = 0;
      for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1] && !cantab; k++) {
        cantab = cutgraph_tabbable(g, &st, g->e + (g->pedge[k] >> 1), WH_HOSTTAB, wv);
      }
      if(!cantab) bad = 1;
      if(!bad) {
        float thick, spike;
        whimsy_measure(body, bn, &thick, &spike);
        if(thick < WH_MINTHICK || spike > WHIMSY_MAXSPIKE) bad = 1;
      }
    }
  }
  free(body);
  free(wv);
  return bad;
}

// Step 6: grid tabs near the whimsy could poke into it; those edges get fresh tabs later
static void whimsy_retab(CutGraph * g, const float * xy, uint32_t n) {
  for(uint32_t i = 0; i < g->ne; i++) {
    CutEdge * e = g->e + i;
    if(!(e->flags & CUTEDGE_GRID)) continue;
    if(whimsy_segdist(xy, n, g->v[e->a].x, g->v[e->a].y, g->v[e->b].x, g->v[e->b].y) < WH_GRIDNEAR) {
      e->flags &= ~CUTEDGE_GRID;
      e->ntabs = 0;
    }
  }
}

int whimsy_insert(CutGraph * g, const float * xy, uint32_t n) {
  WhInsert w = { .xy = xy, .n = n };
  CutGraph old = {0};
  CutEdge * ne = NULL;
  float * pts = NULL;
  int copied = 0;
  int result = whimsy_crossings(g, &w);
  if(result) goto done;
  // From here the graph changes: keep a copy to undo
  result = -1;
  if(cutgraph_copy(&old, g)) goto done;
  copied = 1;
  CutVert * nv = realloc(g->v, sizeof(CutVert) * (g->nv + w.ncross));
  ne = malloc(sizeof(CutEdge) * (g->ne + 2 * w.ncross));
  pts = malloc(sizeof(float) * 2 * (g->npts + n));
  if(nv) g->v = nv;
  if(!nv || !ne || !pts) goto done;
  memcpy(pts, g->pts, sizeof(float) * 2 * g->npts);
  uint32_t nn, np = g->npts;
  int32_t wid = g->count;
  result = whimsy_split(g, &w, ne, &nn);
  if(!result) result = whimsy_arcs(g, &w, wid, ne, &nn, pts, &np);
  if(result) goto done;
  free(g->e);
  free(g->pts);
  g->e = ne;
  g->ne = nn;
  g->pts = pts;
  g->npts = np;
  g->count++;
  ne = NULL;
  pts = NULL;
  result = cutgraph_rewalk(g) ? 1 : whimsy_check(g, wid);
  if(!result) whimsy_retab(g, xy, n);
done:
  free(w.cross);
  free(w.inside);
  free(w.dead);
  free(ne);
  free(pts);
  if(copied) {
    if(result) {
      cutgraph_free(g);
      *g = old;
    } else {
      cutgraph_free(&old);
    }
  }
  return result;
}

int whimsy_add(CutGraph * g, uint8_t level, uint8_t set, uint32_t seed) {
  uint32_t want = whimsy_count(level, g->count);
  if(!want) return 0;
  Rng r;
  rng_init(&r, seed ^ 0x5EEDu);
  float base[WHIMSY_MAXSHAPE * 2], shape[WHIMSY_MAXSHAPE * 2];
  float * cx = malloc(sizeof(float) * 3 * want);
  if(!cx) return -1;
  uint32_t placed = 0, nshapes = whimsy_setsize(set), first = rng_below(&r, nshapes);
  for(uint32_t k = 0; k < want; k++) {
    // This whimsy's shape, or if it fits nowhere, the set's other shapes in turn
    int done = 0;
    for(uint32_t alt = 0; alt < nshapes && !done; alt++) {
      uint32_t n = whimsy_shape(set, first + k + alt, base);
      float rad = 0;
      for(uint32_t i = 0; i < n; i++) rad = fmaxf(rad, sqrtf(base[2 * i] * base[2 * i] + base[2 * i + 1] * base[2 * i + 1]));
      for(int t = 0; t < WHIMSY_TRIES && !done; t++) {
        float sc = sqrtf(WHIMSY_AREA * rng_range(&r, 0.85f, 1.15f));
        float ang = rng_range(&r, -0.35f, 0.35f), ca = cosf(ang), sa = sinf(ang);
        float R = rad * sc, lo = WHIMSY_BORDER + R;
        if(g->width - 2 * lo <= 0 || g->height - 2 * lo <= 0) break;
        // Centered near a corner where pieces meet: the pieces around share the bite
        const CutVert * v = g->v + rng_below(&r, g->nv);
        float x = v->x + rng_range(&r, -WHIMSY_JITTER, WHIMSY_JITTER);
        float y = v->y + rng_range(&r, -WHIMSY_JITTER, WHIMSY_JITTER);
        if(x < lo || y < lo || x > g->width - lo || y > g->height - lo) continue;
        int close = 0;
        for(uint32_t j = 0; j < placed; j++) {
          float dx = x - cx[3 * j], dy = y - cx[3 * j + 1];
          if(sqrtf(dx * dx + dy * dy) < R + cx[3 * j + 2] + WHIMSY_GAP) close = 1;
        }
        if(close) continue;
        for(uint32_t i = 0; i < n; i++) {
          float px = base[2 * i] * sc, py = base[2 * i + 1] * sc;
          shape[2 * i] = x + px * ca - py * sa;
          shape[2 * i + 1] = y + px * sa + py * ca;
        }
        int res = whimsy_insert(g, shape, n);
        if(res < 0) {
          free(cx);
          return -1;
        }
        if(res == 0) {
          cx[3 * placed] = x;
          cx[3 * placed + 1] = y;
          cx[3 * placed + 2] = R;
          placed++;
          done = 1;
        }
      }
    }
  }
  free(cx);
  return placed;
}

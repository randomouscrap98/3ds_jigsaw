#include "randcut.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

static const RandCutStyle randcut_styles[RANDCUT_STYLES] = {
  { .relax = 4, .collapse = 0.12f, .mintab = 0.45f, .twotab = 1.5f, .maxtab = 1.0f },  // Random
};

const RandCutStyle * randcut_style(uint8_t style) {
  return randcut_styles + (style < RANDCUT_STYLES ? style : RANDCUT_STYLE_EVEN);
}

#define RC_MAXCELL 64       // Corners per Voronoi cell while clipping
#define RC_MERGE 1e-3f      // Clipped corners closer than this are the same vertex
#define RC_BUCKET 0.25f     // Vertex merge bucket size
#define RC_MINAREA 0.05f    // Smallest piece area (cells^2) after collapsing

void randcut_free(RandCut * c) {
  free(c->v);
  free(c->e);
  free(c->pstart);
  free(c->pedge);
  memset(c, 0, sizeof(RandCut));
}

// ---- Voronoi cells by half-plane clipping ----

typedef struct {
  float x, y;
} RcPt;

// Clip convex polygon in (n points) to the half-plane (p - m) . d <= 0. Returns point count.
static int randcut_clip(const RcPt * in, int n, RcPt * out, float mx, float my, float dx, float dy) {
  int m = 0;
  for(int i = 0; i < n && m < RC_MAXCELL - 1; i++) {
    const RcPt * p = in + i, * q = in + (i + 1) % n;
    float fp = (p->x - mx) * dx + (p->y - my) * dy, fq = (q->x - mx) * dx + (q->y - my) * dy;
    if(fp <= 0) out[m++] = *p;
    if((fp <= 0) != (fq <= 0)) {
      float t = fp / (fp - fq);
      out[m++] = (RcPt) { p->x + t * (q->x - p->x), p->y + t * (q->y - p->y) };
    }
  }
  return m;
}

typedef struct {
  float d;
  uint32_t j;
} RcNear;

static int randcut_nearcmp(const void * a, const void * b) {
  float x = ((const RcNear *)a)->d, y = ((const RcNear *)b)->d;
  return x < y ? -1 : x > y;
}

// Voronoi cell of site i (clockwise, y down). near is scratch for count entries. Returns
// corner count, or 0 on a degenerate cell.
static int randcut_cell(const RcPt * sites, uint32_t count, uint32_t i, float w, float h,
    RcNear * near, RcPt * poly) {
  RcPt tmp[RC_MAXCELL];
  int n = 4;
  poly[0] = (RcPt) { 0, 0 };
  poly[1] = (RcPt) { w, 0 };
  poly[2] = (RcPt) { w, h };
  poly[3] = (RcPt) { 0, h };
  uint32_t nn = 0;
  for(uint32_t j = 0; j < count; j++) {
    if(j == i) continue;
    float dx = sites[j].x - sites[i].x, dy = sites[j].y - sites[i].y;
    near[nn++] = (RcNear) { dx * dx + dy * dy, j };
  }
  qsort(near, nn, sizeof(RcNear), randcut_nearcmp);
  for(uint32_t k = 0; k < nn; k++) {
    // A site further than twice the cell's reach can't cut it
    float reach = 0;
    for(int v = 0; v < n; v++) {
      float dx = poly[v].x - sites[i].x, dy = poly[v].y - sites[i].y;
      reach = fmaxf(reach, dx * dx + dy * dy);
    }
    if(near[k].d > 4 * reach) break;
    if(near[k].d < 1e-8f) return 0;
    const RcPt * s = sites + near[k].j;
    n = randcut_clip(poly, n, tmp, (sites[i].x + s->x) / 2, (sites[i].y + s->y) / 2,
        s->x - sites[i].x, s->y - sites[i].y);
    if(n < 3) return 0;
    memcpy(poly, tmp, sizeof(RcPt) * n);
  }
  return n;
}

static float randcut_area(const RcPt * p, int n, float * cx, float * cy) {
  double a = 0, x = 0, y = 0;
  for(int i = 0; i < n; i++) {
    const RcPt * u = p + i, * v = p + (i + 1) % n;
    double c = (double)u->x * v->y - (double)v->x * u->y;
    a += c;
    x += (u->x + v->x) * c;
    y += (u->y + v->y) * c;
  }
  a /= 2;
  if(cx && a != 0) {
    *cx = x / (6 * a);
    *cy = y / (6 * a);
  }
  return a;
}

// ---- Topology: shared vertices, collapsed short edges, edges with two sides ----

typedef struct {
  RcPt * cell;          // All cells' corners, cell i at cell[cstart[i]..cstart[i+1])
  uint32_t * cstart;
  uint32_t * vid;       // Vertex id of every corner
  RcPt * vert;
  uint8_t * pin;        // Bits: 1 = x is 0, 2 = x is w, 4 = y is 0, 8 = y is h
  uint32_t * parent;    // Union-find for collapsing
  uint32_t nv;
} RcWork;

static uint32_t randcut_find(uint32_t * parent, uint32_t v) {
  while(parent[v] != v) {
    parent[v] = parent[parent[v]];
    v = parent[v];
  }
  return v;
}

// Give every corner a vertex id, merging corners within RC_MERGE. Returns 0 on success.
static int randcut_vertices(RcWork * w, uint32_t corners, float width, float height) {
  int32_t bw = (int32_t)(width / RC_BUCKET) + 1, bh = (int32_t)(height / RC_BUCKET) + 1;
  int32_t * head = malloc(sizeof(int32_t) * bw * bh);
  int32_t * next = malloc(sizeof(int32_t) * corners);
  w->vert = malloc(sizeof(RcPt) * corners);
  w->pin = malloc(corners);
  w->vid = malloc(sizeof(uint32_t) * corners);
  w->parent = malloc(sizeof(uint32_t) * corners);
  int err = !head || !next || !w->vert || !w->pin || !w->vid || !w->parent;
  if(!err) {
    for(int32_t i = 0; i < bw * bh; i++) head[i] = -1;
    w->nv = 0;
    for(uint32_t k = 0; k < corners; k++) {
      RcPt p = w->cell[k];
      int32_t bx = (int32_t)(p.x / RC_BUCKET), by = (int32_t)(p.y / RC_BUCKET);
      int32_t found = -1;
      for(int32_t y = by - 1; y <= by + 1 && found < 0; y++) {
        for(int32_t x = bx - 1; x <= bx + 1 && found < 0; x++) {
          if(x < 0 || y < 0 || x >= bw || y >= bh) continue;
          for(int32_t v = head[y * bw + x]; v >= 0; v = next[v]) {
            if(fabsf(w->vert[v].x - p.x) < RC_MERGE && fabsf(w->vert[v].y - p.y) < RC_MERGE) {
              found = v;
              break;
            }
          }
        }
      }
      if(found < 0) {
        found = w->nv++;
        uint8_t pin = 0;
        if(p.x < RC_MERGE) { pin |= 1; p.x = 0; }
        if(p.x > width - RC_MERGE) { pin |= 2; p.x = width; }
        if(p.y < RC_MERGE) { pin |= 4; p.y = 0; }
        if(p.y > height - RC_MERGE) { pin |= 8; p.y = height; }
        w->vert[found] = p;
        w->pin[found] = pin;
        w->parent[found] = found;
        if(bx < 0) bx = 0;
        if(by < 0) by = 0;
        if(bx >= bw) bx = bw - 1;
        if(by >= bh) by = bh - 1;
        next[found] = head[by * bw + bx];
        head[by * bw + bx] = found;
      }
      w->vid[k] = found;
    }
  }
  free(head);
  free(next);
  return err;
}

// Collapse edges shorter than len into a point. Border vertices stay on their border.
static void randcut_collapse(RcWork * w, uint32_t count, float len) {
  for(int pass = 0; pass < 10; pass++) {
    int any = 0;
    for(uint32_t i = 0; i < count; i++) {
      uint32_t n = w->cstart[i + 1] - w->cstart[i];
      for(uint32_t k = 0; k < n; k++) {
        uint32_t u = randcut_find(w->parent, w->vid[w->cstart[i] + k]);
        uint32_t v = randcut_find(w->parent, w->vid[w->cstart[i] + (k + 1) % n]);
        if(u == v) continue;
        RcPt a = w->vert[u], b = w->vert[v];
        float dx = b.x - a.x, dy = b.y - a.y;
        if(dx * dx + dy * dy >= len * len) continue;
        uint8_t pu = w->pin[u], pv = w->pin[v];
        // Can't merge points pinned to opposite borders or different sides of a corner
        if((pu & 3) && (pv & 3) && (pu & 3) != (pv & 3)) continue;
        if((pu & 12) && (pv & 12) && (pu & 12) != (pv & 12)) continue;
        RcPt m = { (a.x + b.x) / 2, (a.y + b.y) / 2 };
        if(pu & 3) m.x = a.x; else if(pv & 3) m.x = b.x;
        if(pu & 12) m.y = a.y; else if(pv & 12) m.y = b.y;
        w->parent[v] = u;
        w->vert[u] = m;
        w->pin[u] = pu | pv;
        any = 1;
      }
    }
    if(!any) break;
  }
}

typedef struct {
  uint32_t lo, hi, cell;
  uint8_t rev;
} RcHalf;

static int randcut_halfcmp(const void * a, const void * b) {
  const RcHalf * x = a, * y = b;
  if(x->lo != y->lo) return x->lo < y->lo ? -1 : 1;
  if(x->hi != y->hi) return x->hi < y->hi ? -1 : 1;
  return 0;
}

// Build vertices, edges and piece walks from the collapsed cells. Returns 0 on success.
static int randcut_topology(RandCut * c, RcWork * w) {
  uint32_t count = c->count;
  // Final vertex numbering (roots only)
  uint32_t * remap = malloc(sizeof(uint32_t) * w->nv);
  if(!remap) return 1;
  c->nv = 0;
  for(uint32_t v = 0; v < w->nv; v++) remap[v] = randcut_find(w->parent, v) == v ? c->nv++ : 0;
  c->v = malloc(sizeof(RandCutVert) * c->nv);
  uint32_t corners = w->cstart[count];
  uint32_t * walk = malloc(sizeof(uint32_t) * corners);
  RcHalf * half = malloc(sizeof(RcHalf) * corners);
  c->pstart = malloc(sizeof(uint32_t) * (count + 1));
  int err = !c->v || !walk || !half || !c->pstart;
  if(!err) {
    for(uint32_t v = 0; v < w->nv; v++) {
      if(randcut_find(w->parent, v) == v) c->v[remap[v]] = (RandCutVert) { w->vert[v].x, w->vert[v].y };
    }
    // Piece walks: drop repeated vertices left by collapsing
    uint32_t nw = 0;
    for(uint32_t i = 0; i < count && !err; i++) {
      c->pstart[i] = nw;
      uint32_t n = w->cstart[i + 1] - w->cstart[i];
      for(uint32_t k = 0; k < n; k++) {
        uint32_t v = remap[randcut_find(w->parent, w->vid[w->cstart[i] + k])];
        if(nw > c->pstart[i] && walk[nw - 1] == v) continue;
        walk[nw++] = v;
      }
      while(nw - c->pstart[i] > 1 && walk[nw - 1] == walk[c->pstart[i]]) nw--;
      if(nw - c->pstart[i] < 3) err = 1;
    }
    c->pstart[count] = nw;
    // Area check (collapsing must not leave slivers or flip anything)
    for(uint32_t i = 0; i < count && !err; i++) {
      RcPt p[RC_MAXCELL];
      uint32_t n = c->pstart[i + 1] - c->pstart[i];
      if(n > RC_MAXCELL) { err = 1; break; }
      for(uint32_t k = 0; k < n; k++) p[k] = (RcPt) { c->v[walk[c->pstart[i] + k]].x, c->v[walk[c->pstart[i] + k]].y };
      if(randcut_area(p, n, NULL, NULL) < RC_MINAREA) err = 1;
    }
    // Edges: sort half-edges by vertex pair; each pair is used by one or two pieces
    for(uint32_t i = 0; i < count && !err; i++) {
      uint32_t s = c->pstart[i], n = c->pstart[i + 1] - s;
      for(uint32_t k = 0; k < n; k++) {
        uint32_t a = walk[s + k], b = walk[s + (k + 1) % n];
        half[s + k] = (RcHalf) { a < b ? a : b, a < b ? b : a, i, a > b };
      }
    }
    if(!err) {
      qsort(half, nw, sizeof(RcHalf), randcut_halfcmp);
      c->e = malloc(sizeof(RandCutEdge) * nw);
      c->pedge = malloc(sizeof(uint32_t) * nw);
      if(!c->e || !c->pedge) err = 1;
    }
    c->ne = 0;
    for(uint32_t k = 0; k < nw && !err; ) {
      uint32_t same = 1;
      while(k + same < nw && !randcut_halfcmp(half + k, half + k + same)) same++;
      RandCutEdge * e = c->e + c->ne;
      memset(e, 0, sizeof(RandCutEdge));
      e->a = half[k].lo;
      e->b = half[k].hi;
      e->left = e->right = -1;
      for(uint32_t j = 0; j < same; j++) {
        int32_t * side = half[k + j].rev ? &e->right : &e->left;
        if(*side >= 0) err = 1;   // Two pieces walking the same way: broken topology
        *side = half[k + j].cell;
      }
      if(same > 2) err = 1;
      if(same == 1) {
        // Border edge: both ends must sit on the same border
        RandCutVert a = c->v[e->a], b = c->v[e->b];
        int onborder = (a.x == 0 && b.x == 0) || (a.y == 0 && b.y == 0) ||
            (a.x == c->width && b.x == c->width) || (a.y == c->height && b.y == c->height);
        if(!onborder) err = 1;
      }
      c->ne++;
      k += same;
    }
    // Piece walks as edges
    for(uint32_t i = 0; i < count && !err; i++) {
      uint32_t s = c->pstart[i], n = c->pstart[i + 1] - s;
      for(uint32_t k = 0; k < n; k++) {
        uint32_t a = walk[s + k], b = walk[s + (k + 1) % n];
        RcHalf key = { a < b ? a : b, a < b ? b : a, 0, 0 };
        uint32_t lo = 0, hi = c->ne;
        while(lo < hi) {
          uint32_t mid = (lo + hi) / 2;
          RcHalf mk = { c->e[mid].a, c->e[mid].b, 0, 0 };
          if(randcut_halfcmp(&mk, &key) < 0) lo = mid + 1;
          else hi = mid;
        }
        c->pedge[s + k] = lo * 2 + (a > b);
      }
    }
  }
  free(remap);
  free(walk);
  free(half);
  return err;
}

// ---- Tabs ----

static float randcut_edgelen(const RandCut * c, const RandCutEdge * e) {
  float dx = c->v[e->b].x - c->v[e->a].x, dy = c->v[e->b].y - c->v[e->a].y;
  return sqrtf(dx * dx + dy * dy);
}

// Shapes and positions for an edge's tabs (count already set)
static void randcut_rolltabs(const RandCut * c, const RandCutStyle * st, RandCutEdge * e, Rng * r) {
  const EdgeStyle * es = edgegen_style(EDGEGEN_STYLE_CLASSIC);
  float L = randcut_edgelen(c, e);
  float slot = e->ntabs ? L / e->ntabs : 0;
  for(int k = 0; k < e->ntabs; k++) {
    // A tab is drawn over at most maxtab of the edge; leftover room jitters its position
    float len = slot;
    if(len > st->maxtab) len = st->maxtab;
    e->len[k] = len;
    float pos = (k + 0.5f) * slot + rng_range(r, -0.4f, 0.4f) * (slot - len);
    if(pos < len / 2) pos = len / 2;
    if(pos > L - len / 2) pos = L - len / 2;
    e->pos[k] = pos;
    edgegen_randomtab(r, es, e->tab + k);
  }
}

static int randcut_piecetabs(const RandCut * c, uint32_t p) {
  int n = 0;
  for(uint32_t k = c->pstart[p]; k < c->pstart[p + 1]; k++) n += c->e[c->pedge[k] >> 1].ntabs;
  return n;
}

// Canonical (a -> b) points of an edge in cell units, a included, b excluded
static uint32_t randcut_edgepoints(const RandCut * c, const RandCutEdge * e, float * xy) {
  float ax = c->v[e->a].x, ay = c->v[e->a].y;
  float dx = c->v[e->b].x - ax, dy = c->v[e->b].y - ay;
  float L = sqrtf(dx * dx + dy * dy);
  float ux = dx / L, uy = dy / L, nx = -uy, ny = ux;
  uint32_t n = 0;
  xy[n * 2] = ax; xy[n * 2 + 1] = ay; n++;
  float pts[(3 * EDGEGEN_SEGMENTS + 1) * 2];
  for(int k = 0; k < e->ntabs; k++) {
    int m = edgegen_tabcurve(e->tab + k, pts);
    float s0 = e->pos[k] - e->len[k] / 2;
    for(int i = 0; i < m; i++) {
      float s = s0 + pts[i * 2] * e->len[k], o = pts[i * 2 + 1] * e->len[k];
      xy[n * 2] = ax + s * ux + o * nx;
      xy[n * 2 + 1] = ay + s * uy + o * ny;
      n++;
    }
  }
  return n;
}

uint32_t randcut_maxpoints(const RandCut * c) {
  uint32_t best = 0;
  for(uint32_t p = 0; p < c->count; p++) {
    uint32_t n = 0;
    for(uint32_t k = c->pstart[p]; k < c->pstart[p + 1]; k++) {
      n += 1 + c->e[c->pedge[k] >> 1].ntabs * (3 * EDGEGEN_SEGMENTS + 1);
    }
    if(n > best) best = n;
  }
  return best;
}

// Append a point unless it (nearly) repeats the previous one: a tab spanning a whole edge
// starts on its corner, and rounding can put it a hair to either side, which folds the
// outline back on itself (edgegen_shape_ok sees a crossing)
#define RC_SAMEPOINT 1e-4f   // Cells
static int randcut_same(const float * a, float x, float y, float tol) {
  return fabsf(a[0] - x) <= tol && fabsf(a[1] - y) <= tol;
}

static void randcut_emit(float * xy, uint32_t * n, float x, float y, float tol) {
  if(*n && randcut_same(xy + *n * 2 - 2, x, y, tol)) return;
  xy[*n * 2] = x;
  xy[*n * 2 + 1] = y;
  (*n)++;
}

uint32_t randcut_outline(const RandCut * c, uint32_t p, float cell, float ox, float oy, float * xy) {
  uint32_t n = 0;
  float tol = RC_SAMEPOINT * cell;
  float buf[(2 * (3 * EDGEGEN_SEGMENTS + 1) + 1) * 2];
  for(uint32_t k = c->pstart[p]; k < c->pstart[p + 1]; k++) {
    const RandCutEdge * e = c->e + (c->pedge[k] >> 1);
    uint32_t m = randcut_edgepoints(c, e, buf);
    if(c->pedge[k] & 1) {
      // Walking b -> a: start at b, then the edge's points backwards, stopping before a
      const RandCutVert * b = c->v + e->b;
      randcut_emit(xy, &n, ox + b->x * cell, oy + b->y * cell, tol);
      for(uint32_t i = m - 1; i > 0; i--) randcut_emit(xy, &n, ox + buf[i * 2] * cell, oy + buf[i * 2 + 1] * cell, tol);
    } else {
      for(uint32_t i = 0; i < m; i++) randcut_emit(xy, &n, ox + buf[i * 2] * cell, oy + buf[i * 2 + 1] * cell, tol);
    }
  }
  while(n > 1 && randcut_same(xy + n * 2 - 2, xy[0], xy[1], tol)) n--;
  return n;
}

// Tab counts, then reroll tabs around pieces whose outline fails until all pass.
// Returns 0 on success.
static int randcut_tabs(RandCut * c, const RandCutStyle * st, uint32_t seed) {
  Rng r;
  rng_init(&r, seed ^ 0x7AB5u);
  for(uint32_t i = 0; i < c->ne; i++) {
    RandCutEdge * e = c->e + i;
    float L = randcut_edgelen(c, e);
    e->ntabs = e->left < 0 || e->right < 0 || L < st->mintab ? 0 : L >= st->twotab ? 2 : 1;
    randcut_rolltabs(c, st, e, &r);
  }
  // Every piece needs a tab or blank: give tabless pieces one on their longest inner edge
  for(uint32_t p = 0; p < c->count; p++) {
    if(randcut_piecetabs(c, p)) continue;
    int32_t best = -1;
    float bestlen = 0;
    for(uint32_t k = c->pstart[p]; k < c->pstart[p + 1]; k++) {
      RandCutEdge * e = c->e + (c->pedge[k] >> 1);
      if(e->left < 0 || e->right < 0) continue;
      float L = randcut_edgelen(c, e);
      if(L > bestlen) { bestlen = L; best = c->pedge[k] >> 1; }
    }
    if(best < 0 || bestlen < st->mintab * 0.6f) return 1;
    c->e[best].ntabs = 1;
    randcut_rolltabs(c, st, c->e + best, &r);
  }
  uint32_t maxp = randcut_maxpoints(c);
  float * xy = malloc(sizeof(float) * 2 * maxp);
  uint8_t * check = malloc(c->count);
  if(!xy || !check) {
    free(xy);
    free(check);
    return 1;
  }
  memset(check, 1, c->count);
  int result = 1;
  for(int pass = 0; pass < RANDCUT_MAXPASSES; pass++) {
    int any = 0;
    for(uint32_t p = 0; p < c->count; p++) {
      if(!(check[p] & 1)) continue;
      check[p] &= ~1;
      uint32_t n = randcut_outline(c, p, 1, 0, 0, xy);
      if(edgegen_shape_ok(xy, n, RANDCUT_CLEARANCE)) continue;
      any = 1;
      Rng pr;
      rng_init(&pr, seed ^ (p * 2654435761u) ^ ((uint32_t)(pass + 1) * 0x9E3779B9u));
      for(uint32_t k = c->pstart[p]; k < c->pstart[p + 1]; k++) {
        RandCutEdge * e = c->e + (c->pedge[k] >> 1);
        randcut_rolltabs(c, st, e, &pr);
        if(e->left >= 0) check[e->left] |= 2;
        if(e->right >= 0) check[e->right] |= 2;
      }
    }
    c->passes = pass + 1;
    if(!any) {
      result = 0;
      break;
    }
    for(uint32_t p = 0; p < c->count; p++) check[p] >>= 1;
  }
  free(xy);
  free(check);
  return result;
}

// ---- Whole cut ----

static int randcut_try(RandCut * c, uint32_t seed, const RandCutStyle * st) {
  uint32_t count = c->count;
  float w = c->width, h = c->height;
  RcPt * sites = malloc(sizeof(RcPt) * count);
  RcNear * near = malloc(sizeof(RcNear) * count);
  RcWork wk = {0};
  wk.cstart = malloc(sizeof(uint32_t) * (count + 1));
  wk.cell = malloc(sizeof(RcPt) * count * RC_MAXCELL);
  int err = !sites || !near || !wk.cstart || !wk.cell;
  if(!err) {
    Rng r;
    rng_init(&r, seed);
    for(uint32_t i = 0; i < count; i++) sites[i] = (RcPt) { rng_range(&r, 0, w), rng_range(&r, 0, h) };
    // Voronoi, moving each site to its cell's centroid relax times, then the final cells
    for(int pass = 0; pass <= st->relax && !err; pass++) {
      uint32_t n = 0;
      for(uint32_t i = 0; i < count && !err; i++) {
        wk.cstart[i] = n;
        int m = randcut_cell(sites, count, i, w, h, near, wk.cell + n);
        if(m == 0) err = 1;
        n += m;
      }
      wk.cstart[count] = n;
      if(err || pass == st->relax) break;
      for(uint32_t i = 0; i < count; i++) {
        uint32_t s = wk.cstart[i];
        randcut_area(wk.cell + s, wk.cstart[i + 1] - s, &sites[i].x, &sites[i].y);
      }
    }
  }
  if(!err) err = randcut_vertices(&wk, wk.cstart[count], w, h);
  if(!err) {
    randcut_collapse(&wk, count, st->collapse);
    err = randcut_topology(c, &wk);
  }
  if(!err) err = randcut_tabs(c, st, seed);
  free(sites);
  free(near);
  free(wk.cell);
  free(wk.cstart);
  free(wk.vid);
  free(wk.vert);
  free(wk.pin);
  free(wk.parent);
  return err;
}

int randcut_init(RandCut * c, float width, float height, uint32_t count, uint32_t seed, const RandCutStyle * st) {
  memset(c, 0, sizeof(RandCut));
  if(count < 2 || count > 0xFFF0 || !(width > 0) || !(height > 0)) return 1;
  for(uint32_t t = 0; t < RANDCUT_MAXTRIES; t++) {
    c->count = count;
    c->width = width;
    c->height = height;
    c->tries = t + 1;
    if(!randcut_try(c, seed ^ (t * 0x85EBCA6Bu), st)) return 0;
    uint32_t tries = c->tries;
    randcut_free(c);
    c->tries = tries;
  }
  return 1;
}

int randcut_geo(const RandCut * c, float cell, PieceGeo * g) {
  memset(g, 0, sizeof(PieceGeo));
  uint32_t count = c->count, walk = c->pstart[count];
  g->count = count;
  g->cell = cell;
  g->width = c->width * cell;
  g->height = c->height * cell;
  g->center = malloc(sizeof(float) * 2 * count);
  g->nbstart = malloc(sizeof(uint32_t) * (count + 1));
  g->nb = malloc(sizeof(uint16_t) * (walk ? walk : 1));
  g->hbstart = malloc(sizeof(uint32_t) * (count + 1));
  g->hb = malloc(sizeof(float) * 2 * walk);
  float * xy = malloc(sizeof(float) * 2 * randcut_maxpoints(c));
  if(!g->center || !g->nbstart || !g->nb || !g->hbstart || !g->hb || !xy) {
    free(xy);
    piecegeo_free(g);
    return 1;
  }
  uint32_t nn = 0;
  for(uint32_t p = 0; p < count; p++) {
    uint32_t n = randcut_outline(c, p, cell, 0, 0, xy);
    float x0 = xy[0], y0 = xy[1], x1 = xy[0], y1 = xy[1];
    for(uint32_t i = 1; i < n; i++) {
      x0 = fminf(x0, xy[i * 2]); x1 = fmaxf(x1, xy[i * 2]);
      y0 = fminf(y0, xy[i * 2 + 1]); y1 = fmaxf(y1, xy[i * 2 + 1]);
    }
    g->center[p * 2] = (x0 + x1) / 2;
    g->center[p * 2 + 1] = (y0 + y1) / 2;
    g->nbstart[p] = nn;
    g->hbstart[p] = c->pstart[p];
    for(uint32_t k = c->pstart[p]; k < c->pstart[p + 1]; k++) {
      const RandCutEdge * e = c->e + (c->pedge[k] >> 1);
      const RandCutVert * v = c->v + ((c->pedge[k] & 1) ? e->b : e->a);
      g->hb[k * 2] = v->x * cell;
      g->hb[k * 2 + 1] = v->y * cell;
      if(e->left < 0 || e->right < 0) continue;
      uint16_t other = (uint32_t)e->left == p ? e->right : e->left;
      int dup = 0;
      for(uint32_t i = g->nbstart[p]; i < nn; i++) dup |= g->nb[i] == other;
      if(!dup) g->nb[nn++] = other;
    }
  }
  g->nbstart[count] = nn;
  g->hbstart[count] = walk;
  free(xy);
  if(piecegeo_index(g)) {
    piecegeo_free(g);
    return 1;
  }
  return 0;
}

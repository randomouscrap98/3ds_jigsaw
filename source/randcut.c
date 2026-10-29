#include "randcut.h"
#include "rng.h"
#include "geom.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

static const RandCutStyle randcut_styles[RANDCUT_STYLES] = {
  { .relax = 4, .collapse = 0.12f, .mintab = 0.45f, .twotab = 1.5f, .maxtab = 1.0f },  // Random
  { .relax = 0, .grid = 0.3f, .collapse = 0.33f, .mintab = 0.45f, .twotab = 1.5f, .maxtab = 1.0f },  // Random Rect
  { .relax = 4, .collapse = 0.12f, .mintab = 0.6f, .twotab = 1.5f, .maxtab = 1.0f, .wave = 0.3f, .knobs = CUTKNOBS,
    .clearance = 0.1f },  // Wooden
};

const RandCutStyle * randcut_style(uint8_t style) {
  return randcut_styles + (style < RANDCUT_STYLES ? style : RANDCUT_STYLE_EVEN);
}

#define RC_MAXCELL 64       // Corners per Voronoi cell while clipping
#define RC_MERGE 1e-3f      // Clipped corners closer than this are the same vertex
#define RC_BUCKET 0.25f     // Vertex merge bucket size
#define RC_MINAREA 0.05f    // Smallest piece area (cells^2) after collapsing

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
static int randcut_topology(CutGraph * c, RcWork * w) {
  uint32_t count = c->count;
  // Final vertex numbering (roots only)
  uint32_t * remap = malloc(sizeof(uint32_t) * w->nv);
  if(!remap) return 1;
  c->nv = 0;
  for(uint32_t v = 0; v < w->nv; v++) remap[v] = randcut_find(w->parent, v) == v ? c->nv++ : 0;
  c->v = malloc(sizeof(CutVert) * c->nv);
  uint32_t corners = w->cstart[count];
  uint32_t * walk = malloc(sizeof(uint32_t) * corners);
  RcHalf * half = malloc(sizeof(RcHalf) * corners);
  c->pstart = malloc(sizeof(uint32_t) * (count + 1));
  int err = !c->v || !walk || !half || !c->pstart;
  if(!err) {
    for(uint32_t v = 0; v < w->nv; v++) {
      if(randcut_find(w->parent, v) == v) c->v[remap[v]] = (CutVert) { w->vert[v].x, w->vert[v].y };
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
      if(geom_area((const float *)(p), n, NULL, NULL) < RC_MINAREA) err = 1;
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
      c->e = malloc(sizeof(CutEdge) * nw);
      c->pedge = malloc(sizeof(uint32_t) * nw);
      if(!c->e || !c->pedge) err = 1;
    }
    c->ne = 0;
    for(uint32_t k = 0; k < nw && !err; ) {
      uint32_t same = 1;
      while(k + same < nw && !randcut_halfcmp(half + k, half + k + same)) same++;
      CutEdge * e = c->e + c->ne;
      memset(e, 0, sizeof(CutEdge));
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
        CutVert a = c->v[e->a], b = c->v[e->b];
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

// ---- Whole cut ----

int randcut_graph(CutGraph * c, float w, float h, uint32_t count, uint32_t seed, const RandCutStyle * st) {
  memset(c, 0, sizeof(CutGraph));
  if(count < 2 || count > 0xFFF0 || !(w > 0) || !(h > 0)) return 1;
  c->count = count;
  c->width = w;
  c->height = h;
  RcPt * sites = malloc(sizeof(RcPt) * count);
  RcNear * near = malloc(sizeof(RcNear) * count);
  RcWork wk = {0};
  wk.cstart = malloc(sizeof(uint32_t) * (count + 1));
  wk.cell = malloc(sizeof(RcPt) * count * RC_MAXCELL);
  int err = !sites || !near || !wk.cstart || !wk.cell;
  if(!err) {
    Rng r;
    rng_init(&r, seed);
    if(st->grid > 0) {
      uint32_t cols = (uint32_t)lroundf(w);
      if(cols == 0 || count % cols) err = 1;
      for(uint32_t i = 0; i < count && !err; i++) {
        float x = i % cols + 0.5f, y = i / cols + 0.5f;
        sites[i].x = x + rng_range(&r, -st->grid, st->grid);
        sites[i].y = y + rng_range(&r, -st->grid, st->grid);
      }
    } else {
      for(uint32_t i = 0; i < count; i++) sites[i] = (RcPt) { rng_range(&r, 0, w), rng_range(&r, 0, h) };
    }
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
        geom_area((const float *)(wk.cell + s), wk.cstart[i + 1] - s, &sites[i].x, &sites[i].y);
      }
    }
  }
  if(!err) err = randcut_vertices(&wk, wk.cstart[count], w, h);
  if(!err) {
    randcut_collapse(&wk, count, st->collapse);
    err = randcut_topology(c, &wk);
  }
  free(sites);
  free(near);
  free(wk.cell);
  free(wk.cstart);
  free(wk.vid);
  free(wk.vert);
  free(wk.pin);
  free(wk.parent);
  if(err) cutgraph_free(c);
  return err;
}

void randcut_tabstyle(const RandCutStyle * st, CutTabStyle * ts) {
  ts->mintab = st->mintab;
  ts->twotab = st->twotab;
  ts->maxtab = st->maxtab;
  ts->whimsytab = 0;
  ts->jitter = edgegen_style(EDGEGEN_STYLE_CLASSIC);
  ts->wave = st->wave;
  ts->knobs = st->knobs;
  ts->clearance = st->clearance;
}

uint32_t randcut_tryseed(uint32_t seed, uint32_t t) {
  return seed ^ (t * 0x85EBCA6Bu);
}

int randcut_init(CutGraph * c, float width, float height, uint32_t count, uint32_t seed, const RandCutStyle * st) {
  memset(c, 0, sizeof(CutGraph));
  CutTabStyle ts;
  randcut_tabstyle(st, &ts);
  for(uint32_t t = 0; t < RANDCUT_MAXTRIES; t++) {
    uint32_t s = randcut_tryseed(seed, t);
    if(!randcut_graph(c, width, height, count, s, st)) {
      c->tries = t + 1;
      if(!cutgraph_tabs(c, &ts, s)) return 0;
      cutgraph_free(c);
    }
  }
  c->tries = RANDCUT_MAXTRIES;
  return 1;
}

#include "cutgraph.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

void cutgraph_free(CutGraph * g) {
  free(g->v);
  free(g->e);
  free(g->pts);
  free(g->pstart);
  free(g->pedge);
  memset(g, 0, sizeof(CutGraph));
}

int cutgraph_grid(CutGraph * g, const EdgeGen * eg) {
  memset(g, 0, sizeof(CutGraph));
  uint32_t cols = eg->cols, rows = eg->rows, vw = cols + 1;
  g->count = cols * rows;
  g->width = cols;
  g->height = rows;
  g->nv = vw * (rows + 1);
  uint32_t maxe = cols * (rows + 1) + (cols + 1) * rows;
  g->v = malloc(sizeof(CutVert) * g->nv);
  g->e = calloc(maxe, sizeof(CutEdge));
  g->pts = malloc(sizeof(float) * 2);
  g->pstart = malloc(sizeof(uint32_t) * (g->count + 1));
  g->pedge = malloc(sizeof(uint32_t) * 4 * g->count);
  if(!g->v || !g->e || !g->pts || !g->pstart || !g->pedge) {
    cutgraph_free(g);
    return 1;
  }
  for(uint32_t y = 0; y <= rows; y++) {
    for(uint32_t x = 0; x <= cols; x++) {
      const float * off = eg->corners + 2 * (y * vw + x);
      g->v[y * vw + x] = (CutVert) { x + off[0], y + off[1] };
    }
  }
  // Horizontal edges (row y of corners, x to x + 1): below piece (x, y - 1), above (x, y).
  // The piece below walks them left to right (its top), the piece above right to left.
  uint32_t * hid = malloc(sizeof(uint32_t) * cols * (rows + 1));
  uint32_t * vid = malloc(sizeof(uint32_t) * (cols + 1) * rows);
  if(!hid || !vid) {
    free(hid);
    free(vid);
    cutgraph_free(g);
    return 1;
  }
  for(uint32_t y = 0; y <= rows; y++) {
    for(uint32_t x = 0; x < cols; x++) {
      CutEdge * e = g->e + g->ne;
      e->a = y * vw + x;
      e->b = y * vw + x + 1;
      e->left = y < rows ? (int32_t)(y * cols + x) : -1;
      e->right = y > 0 ? (int32_t)((y - 1) * cols + x) : -1;
      if(y > 0 && y < rows) {
        // edgegen's normal for horizontal edges points down (+y), as ours does for a -> b
        e->flags = CUTEDGE_GRID;
        e->ntabs = 1;
        e->tab[0] = (CutTab) { eg->h[(y - 1) * cols + x], cutgraph_edgelen(g, e) / 2, cutgraph_edgelen(g, e), 1 };
      }
      hid[y * cols + x] = g->ne++;
    }
  }
  // Vertical edges (column x of corners, y to y + 1): the piece to the left walks them
  // top to bottom (its right side)
  for(uint32_t y = 0; y < rows; y++) {
    for(uint32_t x = 0; x <= cols; x++) {
      CutEdge * e = g->e + g->ne;
      e->a = y * vw + x;
      e->b = (y + 1) * vw + x;
      e->left = x > 0 ? (int32_t)(y * cols + x - 1) : -1;
      e->right = x < cols ? (int32_t)(y * cols + x) : -1;
      if(x > 0 && x < cols) {
        // edgegen's normal for vertical edges points right (+x); ours for a -> b (down)
        // points left, so flip
        e->flags = CUTEDGE_GRID;
        e->ntabs = 1;
        EdgeTab t = eg->v[y * (cols - 1) + x - 1];
        t.flip = -t.flip;
        e->tab[0] = (CutTab) { t, cutgraph_edgelen(g, e) / 2, cutgraph_edgelen(g, e), 1 };
      }
      vid[y * vw + x] = g->ne++;
    }
  }
  // Walks: top (forward), right (forward), bottom (reversed), left (reversed)
  for(uint32_t p = 0; p < g->count; p++) {
    uint32_t x = p % cols, y = p / cols;
    g->pstart[p] = 4 * p;
    g->pedge[4 * p] = hid[y * cols + x] * 2;
    g->pedge[4 * p + 1] = vid[y * vw + x + 1] * 2;
    g->pedge[4 * p + 2] = hid[(y + 1) * cols + x] * 2 + 1;
    g->pedge[4 * p + 3] = vid[y * vw + x] * 2 + 1;
  }
  g->pstart[g->count] = 4 * g->count;
  free(hid);
  free(vid);
  return 0;
}

static void * cutgraph_dup(const void * src, size_t size) {
  void * p = malloc(size ? size : 1);
  if(p && size) memcpy(p, src, size);
  return p;
}

int cutgraph_copy(CutGraph * dst, const CutGraph * src) {
  *dst = *src;
  uint32_t walk = src->pstart ? src->pstart[src->count] : 0;
  dst->v = cutgraph_dup(src->v, sizeof(CutVert) * src->nv);
  dst->e = cutgraph_dup(src->e, sizeof(CutEdge) * src->ne);
  dst->pts = cutgraph_dup(src->pts, sizeof(float) * 2 * src->npts);
  dst->pstart = cutgraph_dup(src->pstart, sizeof(uint32_t) * (src->count + 1));
  dst->pedge = cutgraph_dup(src->pedge, sizeof(uint32_t) * walk);
  if(!dst->v || !dst->e || !dst->pts || !dst->pstart || !dst->pedge) {
    cutgraph_free(dst);
    return 1;
  }
  return 0;
}

float cutgraph_edgelen(const CutGraph * g, const CutEdge * e) {
  float dx = g->v[e->b].x - g->v[e->a].x, dy = g->v[e->b].y - g->v[e->a].y;
  return sqrtf(dx * dx + dy * dy);
}

int cutgraph_iswhimsy(const CutGraph * g, uint32_t p) {
  for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
    if(!(g->e[g->pedge[k] >> 1].flags & CUTEDGE_WHIMSY)) return 0;
  }
  return 1;
}

// ---- Tabs ----

// Shapes and positions for an edge's tabs (count already set). Grid edges keep their
// place and size; only the shape changes.
static void cutgraph_rolltabs(const CutGraph * g, const CutTabStyle * st, CutEdge * e, Rng * r) {
  if(e->flags & CUTEDGE_GRID) {
    for(int k = 0; k < e->ntabs; k++) edgegen_randomtab(r, st->jitter, &e->tab[k].tab);
    return;
  }
  float L = cutgraph_edgelen(g, e);
  float slot = e->ntabs ? L / e->ntabs : 0;
  for(int k = 0; k < e->ntabs; k++) {
    // A tab is drawn over at most maxtab of the edge; leftover room jitters its position
    float len = slot;
    if(len > st->maxtab) len = st->maxtab;
    for(int i = 0; i < e->shrink; i++) len *= CUTGRAPH_SHRINK;
    CutTab * t = e->tab + k;
    t->len = len;
    t->depth = len;
    float pos = (k + 0.5f) * slot + rng_range(r, -0.4f, 0.4f) * (slot - len);
    if(pos < len / 2) pos = len / 2;
    if(pos > L - len / 2) pos = L - len / 2;
    t->pos = pos;
    edgegen_randomtab(r, st->jitter, &t->tab);
  }
}

static int cutgraph_piecetabs(const CutGraph * g, uint32_t p) {
  int n = 0;
  for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) n += g->e[g->pedge[k] >> 1].ntabs;
  return n;
}

// An edge that can carry tabs: between two pieces, straight, not part of a whimsy
static int cutgraph_inner(const CutEdge * e) {
  return e->left >= 0 && e->right >= 0 && e->npts == 0 && !(e->flags & CUTEDGE_WHIMSY);
}

// Does a whimsy's outline end at vertex v? (Only whimsy outline edges have polylines or
// the WHIMSY flag, and they always meet straight edges at crossing vertices.)
static int cutgraph_whimsyvert(const CutGraph * g, uint32_t v) {
  for(uint32_t i = 0; i < g->ne; i++) {
    const CutEdge * e = g->e + i;
    if((e->flags & CUTEDGE_WHIMSY) && (e->a == v || e->b == v)) return 1;
  }
  return 0;
}

int cutgraph_tabbable(const CutGraph * g, const CutTabStyle * st, const CutEdge * e, float min) {
  if(!cutgraph_inner(e)) return 0;
  float L = cutgraph_edgelen(g, e);
  if(L < min) return 0;
  if(st->whimsytab > min && L < st->whimsytab && (cutgraph_whimsyvert(g, e->a) || cutgraph_whimsyvert(g, e->b))) return 0;
  return 1;
}

// Canonical (a -> b) points of an edge in cell units, a included, b excluded
static uint32_t cutgraph_edgepoints(const CutGraph * g, const CutEdge * e, int tabs, float * xy) {
  float ax = g->v[e->a].x, ay = g->v[e->a].y;
  uint32_t n = 0;
  xy[n * 2] = ax; xy[n * 2 + 1] = ay; n++;
  if(e->npts) {
    memcpy(xy + 2, g->pts + 2 * e->p0, sizeof(float) * 2 * e->npts);
    return 1 + e->npts;
  }
  if(!tabs || !e->ntabs) return n;
  float dx = g->v[e->b].x - ax, dy = g->v[e->b].y - ay;
  float L = sqrtf(dx * dx + dy * dy);
  float ux = dx / L, uy = dy / L, nx = -uy, ny = ux;
  float pts[(3 * EDGEGEN_SEGMENTS + 1) * 2];
  for(int k = 0; k < e->ntabs; k++) {
    const CutTab * t = e->tab + k;
    int m = edgegen_tabcurve(&t->tab, pts);
    float s0 = t->pos - t->len / 2;
    for(int i = 0; i < m; i++) {
      float s = s0 + pts[i * 2] * t->len, o = pts[i * 2 + 1] * t->depth;
      xy[n * 2] = ax + s * ux + o * nx;
      xy[n * 2 + 1] = ay + s * uy + o * ny;
      n++;
    }
  }
  return n;
}

static uint32_t cutgraph_edgemax(const CutEdge * e) {
  return 1 + e->npts + e->ntabs * (3 * EDGEGEN_SEGMENTS + 1);
}

uint32_t cutgraph_maxpoints(const CutGraph * g) {
  uint32_t best = 0;
  for(uint32_t p = 0; p < g->count; p++) {
    uint32_t n = 0;
    for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) n += cutgraph_edgemax(g->e + (g->pedge[k] >> 1));
    if(n > best) best = n;
  }
  return best;
}

// Append a point unless it (nearly) repeats the previous one: a tab spanning a whole edge
// starts on its corner, and rounding can put it a hair to either side, which folds the
// outline back on itself (edgegen_shape_ok sees a crossing)
#define CG_SAMEPOINT 1e-4f   // Cells
static int cutgraph_same(const float * a, float x, float y, float tol) {
  return fabsf(a[0] - x) <= tol && fabsf(a[1] - y) <= tol;
}

static void cutgraph_emit(float * xy, uint32_t * n, float x, float y, float tol) {
  if(*n && cutgraph_same(xy + *n * 2 - 2, x, y, tol)) return;
  xy[*n * 2] = x;
  xy[*n * 2 + 1] = y;
  (*n)++;
}

static uint32_t cutgraph_walk(const CutGraph * g, uint32_t p, int tabs, float cell, float ox, float oy, float * xy) {
  uint32_t n = 0, bufmax = 0;
  for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
    uint32_t m = cutgraph_edgemax(g->e + (g->pedge[k] >> 1));
    if(m > bufmax) bufmax = m;
  }
  float stackbuf[(2 * (3 * EDGEGEN_SEGMENTS + 1) + 1) * 2];
  float * buf = bufmax * 2 <= sizeof(stackbuf) / sizeof(float) ? stackbuf : malloc(sizeof(float) * 2 * bufmax);
  if(!buf) return 0;
  float tol = CG_SAMEPOINT * cell;
  for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
    const CutEdge * e = g->e + (g->pedge[k] >> 1);
    uint32_t m = cutgraph_edgepoints(g, e, tabs, buf);
    if(g->pedge[k] & 1) {
      // Walking b -> a: start at b, then the edge's points backwards, stopping before a
      const CutVert * b = g->v + e->b;
      cutgraph_emit(xy, &n, ox + b->x * cell, oy + b->y * cell, tol);
      for(uint32_t i = m - 1; i > 0; i--) cutgraph_emit(xy, &n, ox + buf[i * 2] * cell, oy + buf[i * 2 + 1] * cell, tol);
    } else {
      for(uint32_t i = 0; i < m; i++) cutgraph_emit(xy, &n, ox + buf[i * 2] * cell, oy + buf[i * 2 + 1] * cell, tol);
    }
  }
  while(n > 1 && cutgraph_same(xy + n * 2 - 2, xy[0], xy[1], tol)) n--;
  if(buf != stackbuf) free(buf);
  return n;
}

uint32_t cutgraph_outline(const CutGraph * g, uint32_t p, float cell, float ox, float oy, float * xy) {
  return cutgraph_walk(g, p, 1, cell, ox, oy, xy);
}

uint32_t cutgraph_body(const CutGraph * g, uint32_t p, float cell, float ox, float oy, float * xy) {
  return cutgraph_walk(g, p, 0, cell, ox, oy, xy);
}

int cutgraph_tabs(CutGraph * g, const CutTabStyle * st, uint32_t seed) {
  Rng r;
  rng_init(&r, seed ^ 0x7AB5u);
  for(uint32_t i = 0; i < g->ne; i++) {
    CutEdge * e = g->e + i;
    if(e->flags & (CUTEDGE_GRID | CUTEDGE_WHIMSY)) continue;
    float L = cutgraph_edgelen(g, e);
    e->ntabs = !cutgraph_tabbable(g, st, e, st->mintab) ? 0 : L >= st->twotab ? 2 : 1;
    cutgraph_rolltabs(g, st, e, &r);
  }
  // Every piece but a whimsy needs a tab or blank: give tabless pieces one on their longest
  // inner edge
  for(uint32_t p = 0; p < g->count; p++) {
    if(cutgraph_piecetabs(g, p) || cutgraph_iswhimsy(g, p)) continue;
    int32_t best = -1;
    float bestlen = 0;
    for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
      CutEdge * e = g->e + (g->pedge[k] >> 1);
      if(!cutgraph_tabbable(g, st, e, st->mintab * 0.6f)) continue;
      float L = cutgraph_edgelen(g, e);
      if(L > bestlen) { bestlen = L; best = g->pedge[k] >> 1; }
    }
    if(best < 0) return 1;
    g->e[best].ntabs = 1;
    g->e[best].flags &= ~CUTEDGE_GRID;
    cutgraph_rolltabs(g, st, g->e + best, &r);
  }
  uint32_t maxp = cutgraph_maxpoints(g);
  float * xy = malloc(sizeof(float) * 2 * maxp);
  uint8_t * check = malloc(g->count);
  if(!xy || !check) {
    free(xy);
    free(check);
    return 1;
  }
  memset(check, 1, g->count);
  int result = 1;
  for(int pass = 0; pass < CUTGRAPH_MAXPASSES; pass++) {
    int any = 0;
    for(uint32_t p = 0; p < g->count; p++) {
      if(!(check[p] & 1)) continue;
      check[p] &= ~1;
      uint32_t n = cutgraph_outline(g, p, 1, 0, 0, xy);
      if(edgegen_shape_ok(xy, n, CUTGRAPH_CLEARANCE)) continue;
      any = 1;
      // Rerolling isn't working: give up a tab on the shortest tabbed edge, as long as
      // both pieces keep one (pieces next to whimsies can be too cramped for all of them)
      if(pass >= CUTGRAPH_FALLBACK) {
        int32_t drop = -1;
        float droplen = 1e30f;
        for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
          CutEdge * e = g->e + (g->pedge[k] >> 1);
          if(!e->ntabs || cutgraph_piecetabs(g, e->left) <= e->ntabs || cutgraph_piecetabs(g, e->right) <= e->ntabs) continue;
          float L = cutgraph_edgelen(g, e);
          if(L < droplen) { droplen = L; drop = g->pedge[k] >> 1; }
        }
        if(drop >= 0) {
          CutEdge * e = g->e + drop;
          e->ntabs = 0;
          check[e->left] |= 2;
          check[e->right] |= 2;
          continue;
        }
        // Can't drop one: make this piece's tabs smaller, so they have room to move
        for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
          CutEdge * e = g->e + (g->pedge[k] >> 1);
          if(!e->ntabs || e->shrink >= CUTGRAPH_MAXSHRINK) continue;
          e->flags &= ~CUTEDGE_GRID;
          e->shrink++;
        }
      }
      Rng pr;
      rng_init(&pr, seed ^ (p * 2654435761u) ^ ((uint32_t)(pass + 1) * 0x9E3779B9u));
      for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
        CutEdge * e = g->e + (g->pedge[k] >> 1);
        cutgraph_rolltabs(g, st, e, &pr);
        if(e->left >= 0) check[e->left] |= 2;
        if(e->right >= 0) check[e->right] |= 2;
      }
    }
    g->passes = pass + 1;
    if(!any) {
      result = 0;
      break;
    }
    for(uint32_t p = 0; p < g->count; p++) check[p] >>= 1;
  }
  free(xy);
  free(check);
  return result;
}

int cutgraph_geo(const CutGraph * g, float cell, PieceGeo * pg) {
  memset(pg, 0, sizeof(PieceGeo));
  uint32_t count = g->count, walk = g->pstart[count];
  uint32_t nhb = walk + g->npts * 2;   // Body points: corners + polyline points (each used twice)
  pg->count = count;
  pg->cell = cell;
  pg->width = g->width * cell;
  pg->height = g->height * cell;
  pg->center = malloc(sizeof(float) * 2 * count);
  pg->nbstart = malloc(sizeof(uint32_t) * (count + 1));
  pg->nb = malloc(sizeof(uint16_t) * (walk ? walk : 1));
  pg->hbstart = malloc(sizeof(uint32_t) * (count + 1));
  pg->hb = malloc(sizeof(float) * 2 * nhb);
  float * xy = malloc(sizeof(float) * 2 * cutgraph_maxpoints(g));
  if(!pg->center || !pg->nbstart || !pg->nb || !pg->hbstart || !pg->hb || !xy) {
    free(xy);
    piecegeo_free(pg);
    return 1;
  }
  uint32_t nn = 0, nh = 0;
  for(uint32_t p = 0; p < count; p++) {
    uint32_t n = cutgraph_outline(g, p, cell, 0, 0, xy);
    float x0 = xy[0], y0 = xy[1], x1 = xy[0], y1 = xy[1];
    for(uint32_t i = 1; i < n; i++) {
      x0 = fminf(x0, xy[i * 2]); x1 = fmaxf(x1, xy[i * 2]);
      y0 = fminf(y0, xy[i * 2 + 1]); y1 = fmaxf(y1, xy[i * 2 + 1]);
    }
    pg->center[p * 2] = (x0 + x1) / 2;
    pg->center[p * 2 + 1] = (y0 + y1) / 2;
    pg->hbstart[p] = nh;
    nh += cutgraph_body(g, p, cell, 0, 0, pg->hb + 2 * nh);
    pg->nbstart[p] = nn;
    for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
      const CutEdge * e = g->e + (g->pedge[k] >> 1);
      if(e->left < 0 || e->right < 0) continue;
      uint16_t other = (uint32_t)e->left == p ? e->right : e->left;
      int dup = 0;
      for(uint32_t i = pg->nbstart[p]; i < nn; i++) dup |= pg->nb[i] == other;
      if(!dup) pg->nb[nn++] = other;
    }
  }
  pg->nbstart[count] = nn;
  pg->hbstart[count] = nh;
  free(xy);
  if(piecegeo_index(pg)) {
    piecegeo_free(pg);
    return 1;
  }
  return 0;
}

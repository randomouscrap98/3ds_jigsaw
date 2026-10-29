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
        e->tab[0] = (CutTab) { eg->h[(y - 1) * cols + x], cutgraph_edgelen(g, e) / 2, cutgraph_edgelen(g, e), 1, CUTKNOB_CLASSIC };
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
        e->tab[0] = (CutTab) { t, cutgraph_edgelen(g, e) / 2, cutgraph_edgelen(g, e), 1, CUTKNOB_CLASSIC };
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

// Length along the edge: the chord for straight edges, through the points for polylines
static float cutgraph_arclen(const CutGraph * g, const CutEdge * e) {
  if(!e->npts) return cutgraph_edgelen(g, e);
  float L = 0, px = g->v[e->a].x, py = g->v[e->a].y;
  for(uint32_t i = 0; i <= e->npts; i++) {
    float qx = i < e->npts ? g->pts[2 * (e->p0 + i)] : g->v[e->b].x;
    float qy = i < e->npts ? g->pts[2 * (e->p0 + i) + 1] : g->v[e->b].y;
    L += hypotf(qx - px, qy - py);
    px = qx;
    py = qy;
  }
  return L;
}

int cutgraph_iswhimsy(const CutGraph * g, uint32_t p) {
  for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
    if(!(g->e[g->pedge[k] >> 1].flags & CUTEDGE_WHIMSY)) return 0;
  }
  return 1;
}

// ---- Tabs ----

static int cutgraph_inner(const CutEdge * e);

// Shapes and positions for an edge's tabs (count already set). Grid edges keep their
// place and size; only the shape changes.
static void cutgraph_rolltabs(const CutGraph * g, const CutTabStyle * st, CutEdge * e, Rng * r) {
  if(e->flags & CUTEDGE_GRID) {
    for(int k = 0; k < e->ntabs; k++) edgegen_randomtab(r, st->jitter, &e->tab[k].tab);
    return;
  }
  float L = cutgraph_arclen(g, e);
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
  // Drawn last, and only when used, so cuts without them stay as they were
  if(st->knoblen > 0 || st->knobsize > 0) {
    // Fixed size knobs (not scaled to the edge), calmer shapes
    for(int k = 0; k < e->ntabs; k++) {
      CutTab * t = e->tab + k;
      if(st->knoblen > 0) {
        float len = st->knoblen * rng_range(r, 1 - CUTGRAPH_KNOBVARY, 1 + CUTGRAPH_KNOBVARY);
        for(int i = 0; i < e->shrink; i++) len *= CUTGRAPH_SHRINK;
        if(len > slot * 0.95f) len = slot * 0.95f;
        t->len = t->depth = len;
        float pos = (k + 0.5f) * slot + rng_range(r, -0.4f, 0.4f) * (slot - len);
        if(pos < len / 2) pos = len / 2;
        if(pos > L - len / 2) pos = L - len / 2;
        t->pos = pos;
      }
      if(st->knobsize > 0) {
        t->tab.size = st->knobsize * rng_range(r, 1 - CUTGRAPH_KNOBVARY, 1 + CUTGRAPH_KNOBVARY);
        t->tab.a *= 0.5f; t->tab.b *= 0.5f; t->tab.c *= 0.5f; t->tab.d *= 0.5f; t->tab.e *= 0.5f;
      }
    }
  }
  if(st->onekind) {
    for(int k = 0; k < e->ntabs; k++) e->tab[k].kind = st->onekind;
  } else if(st->knobs > 1) {
    for(int k = 0; k < e->ntabs; k++) e->tab[k].kind = 1 + rng_below(r, st->knobs - 1);
  }
  if(st->curls > 0) {
    // Curls scale with their tab: on short edges, or shrunk to fit, they get too thin
    int fits = e->shrink == 0 && e->ntabs == 1 && !(e->flags & CUTEDGE_NOCURL) &&
        cutgraph_arclen(g, e) >= CUTGRAPH_CURL_MINEDGE;
    for(int k = 0; k < e->ntabs; k++) {
      if(rng_range(r, 0, 1) < st->curls && fits) e->tab[k].kind = CUTKNOB_CURL;
    }
  }
  if(st->swirls > 0) {
    // Swirls have a fixed size (they need it to stay thick): long single-knob edges only
    float L = cutgraph_arclen(g, e);
    int fits = e->shrink == 0 && e->ntabs == 1 && !(e->flags & CUTEDGE_NOCURL) && L >= CUTGRAPH_SWIRL_MINEDGE;
    if(rng_range(r, 0, 1) < st->swirls && fits) {
      CutTab * t = e->tab;
      t->kind = CUTKNOB_SWIRL;
      t->len = t->depth = CUTGRAPH_SWIRL_SPAN;
      float pos = L / 2 + rng_range(r, -0.5f, 0.5f) * (L - CUTGRAPH_SWIRL_SPAN) * 0.6f;
      if(pos < t->len / 2) pos = t->len / 2;
      if(pos > L - t->len / 2) pos = L - t->len / 2;
      t->pos = pos;
    }
  }
  if(st->bend > 0 && (e->dir[0] != 0 || e->dir[1] != 0)) {
    float b = st->bend;
    for(int i = 0; i < e->shrink; i++) b *= CUTGRAPH_SHRINK;
    e->bend = rng_range(r, b * CUTGRAPH_BENDMIN, b);
  }
  if(st->wave > 0 && cutgraph_inner(e) && !e->npts && cutgraph_edgelen(g, e) >= CUTGRAPH_WAVEMIN) {
    float w = st->wave;
    for(int i = 0; i < e->shrink; i++) w *= CUTGRAPH_SHRINK;
    // A bump bends a knob on top of it (its sides fan and the neck pinches), while an S is
    // straight in the middle and only tilts it: edges with a knob get mostly S, with two
    // knobs gentle waves only
    static const float bump[3] = { 1.0f, CUTGRAPH_WAVEKNOB, CUTGRAPH_WAVETWO }, ess[3] = { 1.0f, 1.0f, CUTGRAPH_WAVETWO };
    int nt = e->ntabs < 2 ? e->ntabs : 2;
    e->wave[0] = rng_range(r, -w * bump[nt], w * bump[nt]);
    e->wave[1] = rng_range(r, -w * ess[nt], w * ess[nt]);
  }
}

static int cutgraph_touchwhimsy(const CutGraph * g, uint32_t p) {
  for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
    if(g->e[g->pedge[k] >> 1].flags & CUTEDGE_WHIMSY) return 1;
  }
  return 0;
}

static int cutgraph_piecetabs(const CutGraph * g, uint32_t p) {
  int n = 0;
  for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) n += g->e[g->pedge[k] >> 1].ntabs;
  return n;
}

// An edge that can carry tabs: between two pieces, not part of a whimsy (straight, or a
// polyline from a stroke cut)
static int cutgraph_inner(const CutEdge * e) {
  return e->left >= 0 && e->right >= 0 && !(e->flags & (CUTEDGE_WHIMSY | CUTEDGE_SNAKE));
}

// A piece with a snake is held by it (needs no tab)
static int cutgraph_snaked(const CutGraph * g, uint32_t p) {
  for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
    if(g->e[g->pedge[k] >> 1].flags & CUTEDGE_SNAKE) return 1;
  }
  return 0;
}

uint8_t * cutgraph_whimsyverts(const CutGraph * g) {
  uint8_t * wv = calloc(g->nv ? g->nv : 1, 1);
  if(!wv) return NULL;
  for(uint32_t i = 0; i < g->ne; i++) {
    const CutEdge * e = g->e + i;
    if(e->flags & CUTEDGE_WHIMSY) wv[e->a] = wv[e->b] = 1;
  }
  return wv;
}

int cutgraph_tabbable(const CutGraph * g, const CutTabStyle * st, const CutEdge * e, float min, const uint8_t * wv) {
  if(!cutgraph_inner(e)) return 0;
  float L = cutgraph_arclen(g, e);
  if(L < min) return 0;
  if(wv && L < st->whimsytab && (wv[e->a] || wv[e->b])) return 0;
  return 1;
}

int cutgraph_rewalk(CutGraph * g) {
  uint32_t count = g->count;
  uint32_t total = 0;
  for(uint32_t i = 0; i < g->ne; i++) total += (g->e[i].left >= 0) + (g->e[i].right >= 0);
  // Half-edges bucketed by piece: piece p walks half[start[p]..start[p+1])
  uint32_t * start = calloc(count + 1, sizeof(uint32_t));
  uint32_t * fill = malloc(sizeof(uint32_t) * (count ? count : 1));
  uint32_t * half = malloc(sizeof(uint32_t) * (total ? total : 1));
  uint32_t * walk = malloc(sizeof(uint32_t) * (total ? total : 1));
  uint8_t * used = calloc(total ? total : 1, 1);
  int err = !start || !fill || !half || !walk || !used ? -1 : 0;
  if(!err) {
    for(uint32_t i = 0; i < g->ne; i++) {
      if(g->e[i].left >= 0) start[g->e[i].left + 1]++;
      if(g->e[i].right >= 0) start[g->e[i].right + 1]++;
    }
    for(uint32_t p = 0; p < count; p++) start[p + 1] += start[p];
    memcpy(fill, start, sizeof(uint32_t) * count);
    for(uint32_t i = 0; i < g->ne; i++) {
      if(g->e[i].left >= 0) half[fill[g->e[i].left]++] = i * 2;
      if(g->e[i].right >= 0) half[fill[g->e[i].right]++] = i * 2 + 1;
    }
    uint32_t nw = 0;
    for(uint32_t p = 0; p < count && !err; p++) {
      uint32_t s = start[p], n = start[p + 1] - s;
      if(n < 2) { err = 1; break; }
      // Chain: each half-edge's end is exactly one other half-edge's start
      uint32_t cur = 0;
      for(uint32_t k = 0; k < n && !err; k++) {
        uint32_t h = half[s + cur];
        used[s + cur] = 1;
        walk[nw++] = h;
        const CutEdge * e = g->e + (h >> 1);
        uint32_t end = (h & 1) ? e->a : e->b;
        int32_t next = -1;
        for(uint32_t j = 0; j < n; j++) {
          uint32_t hj = half[s + j];
          const CutEdge * f = g->e + (hj >> 1);
          if(((hj & 1) ? f->b : f->a) != end) continue;
          if(next >= 0) err = 1;   // Pinched: two ways on
          next = j;
        }
        if(next < 0) err = 1;
        else if(k + 1 < n) {
          if(used[s + next]) err = 1;   // Closed early: the piece is in two parts
          cur = next;
        } else if(next != 0) err = 1;
      }
    }
    if(!err) {
      free(g->pstart);
      free(g->pedge);
      g->pstart = start;
      g->pedge = walk;
      start = walk = NULL;
    }
  }
  free(start);
  free(fill);
  free(half);
  free(walk);
  free(used);
  return err;
}

int cutgraph_thick_ok(const float * xy, uint32_t n, float cl) {
  if(n < 3) return 0;
  float stackarc[512];
  float * arc = n <= 512 ? stackarc : malloc(sizeof(float) * n);
  if(!arc) return 0;
  // arc[i] = distance along the outline from point 0 to point i
  arc[0] = 0;
  for(uint32_t i = 1; i < n; i++) {
    arc[i] = arc[i - 1] + hypotf(xy[2 * i] - xy[2 * i - 2], xy[2 * i + 1] - xy[2 * i - 1]);
  }
  float total = arc[n - 1] + hypotf(xy[0] - xy[2 * n - 2], xy[1] - xy[2 * n - 1]);
  float cl2 = cl * cl, far = CUTGRAPH_PINCH * cl;
  int ok = 1;
  for(uint32_t i = 0; i < n && ok; i++) {
    for(uint32_t j = i + 1; j < n; j++) {
      float along = arc[j] - arc[i];
      if(along > total - along) along = total - along;
      if(along <= far) continue;
      float dx = xy[2 * j] - xy[2 * i], dy = xy[2 * j + 1] - xy[2 * i + 1];
      if(dx * dx + dy * dy < cl2) {
        ok = 0;
        break;
      }
    }
  }
  if(arc != stackarc) free(arc);
  return ok;
}

// Waved or stroke-curved: drawn by sampling cutgraph_waveat
static int cutgraph_waved(const CutEdge * e) {
  return e->wave[0] != 0 || e->wave[1] != 0 || e->bend > 0;
}

// Point and unit normal of a waved (or stroke-curved) edge's line at s cells from a (a, unit
// direction u, normal (-uy, ux), chord length L)
static void cutgraph_waveat(const CutEdge * e, float ax, float ay, float ux, float uy, float L, float s,
    float * px, float * py, float * qx, float * qy) {
  if(e->bend > 0) {
    // Cubic Hermite from a (tangent bend L dir_a) to b (arriving along -dir_b)
    float t = s / L, t2 = t * t, t3 = t2 * t, m = e->bend * L;
    float bx = ax + L * ux, by = ay + L * uy;
    float m0x = m * e->dir[0], m0y = m * e->dir[1], m1x = -m * e->dir[2], m1y = -m * e->dir[3];
    float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
    *px = h00 * ax + h10 * m0x + h01 * bx + h11 * m1x;
    *py = h00 * ay + h10 * m0y + h01 * by + h11 * m1y;
    float d00 = 6 * t2 - 6 * t, d10 = 3 * t2 - 4 * t + 1, d01 = -6 * t2 + 6 * t, d11 = 3 * t2 - 2 * t;
    float tx = d00 * ax + d10 * m0x + d01 * bx + d11 * m1x, ty = d00 * ay + d10 * m0y + d01 * by + d11 * m1y;
    float tl = sqrtf(tx * tx + ty * ty);
    if(tl < 1e-9f) { tx = ux; ty = uy; tl = 1; }
    *qx = -ty / tl;
    *qy = tx / tl;
    return;
  }
  float u = (float)M_PI * s / L, w0 = e->wave[0], w1 = e->wave[1];
  float sn = sinf(u), cs = cosf(u);
  float off = L * sn * sn * (w0 + w1 * cs);
  float slope = (float)M_PI * (2 * sn * cs * (w0 + w1 * cs) - w1 * sn * sn * sn);   // d off / d s
  float nx = -uy, ny = ux;
  *px = ax + s * ux + off * nx;
  *py = ay + s * uy + off * ny;
  float tx = ux + slope * nx, ty = uy + slope * ny, tl = sqrtf(tx * tx + ty * ty);
  *qx = -ty / tl;
  *qy = tx / tl;
}

// Waved edge points (a included, b excluded): the line sampled CUTGRAPH_WAVESEG times (with
// tabs replacing the samples they cover), or without tabs CUTGRAPH_WAVEBODY times
static uint32_t cutgraph_wavepoints(const CutGraph * g, const CutEdge * e, int tabs, float * xy) {
  float ax = g->v[e->a].x, ay = g->v[e->a].y;
  float dx = g->v[e->b].x - ax, dy = g->v[e->b].y - ay;
  float L = sqrtf(dx * dx + dy * dy);
  float ux = dx / L, uy = dy / L, px, py, qx, qy;
  uint32_t n = 0;
  xy[n * 2] = ax; xy[n * 2 + 1] = ay; n++;
  int i = 1, ntabs = tabs ? e->ntabs : 0, seg = tabs ? CUTGRAPH_WAVESEG : CUTGRAPH_WAVEBODY;
  float step = L / seg;
  float pts[KNOB_MAXPOINTS * 2];
  for(int k = 0; k < ntabs; k++) {
    const CutTab * t = e->tab + k;
    float s0 = t->pos - t->len / 2, s1 = t->pos + t->len / 2;
    for(; i < seg && i * step < s0; i++) {
      cutgraph_waveat(e, ax, ay, ux, uy, L, i * step, &px, &py, &qx, &qy);
      xy[n * 2] = px; xy[n * 2 + 1] = py; n++;
    }
    int m = knob_curve(t->kind, &t->tab, pts);
    for(int j = 0; j < m; j++) {
      float o = pts[j * 2 + 1] * t->depth;
      cutgraph_waveat(e, ax, ay, ux, uy, L, s0 + pts[j * 2] * t->len, &px, &py, &qx, &qy);
      xy[n * 2] = px + o * qx; xy[n * 2 + 1] = py + o * qy; n++;
    }
    while(i < seg && i * step <= s1) i++;
  }
  for(; i < seg; i++) {
    cutgraph_waveat(e, ax, ay, ux, uy, L, i * step, &px, &py, &qx, &qy);
    xy[n * 2] = px; xy[n * 2 + 1] = py; n++;
  }
  return n;
}

// Point i of a polyline edge (0 = a, npts + 1 = b)
static void cutgraph_polypt(const CutGraph * g, const CutEdge * e, uint32_t i, float * x, float * y) {
  if(i == 0) { *x = g->v[e->a].x; *y = g->v[e->a].y; }
  else if(i > e->npts) { *x = g->v[e->b].x; *y = g->v[e->b].y; }
  else { *x = g->pts[2 * (e->p0 + i - 1)]; *y = g->pts[2 * (e->p0 + i - 1) + 1]; }
}

// Unit normal (-dy, dx) at polyline point i: the average of the steps on either side
static void cutgraph_polynormal(const CutGraph * g, const CutEdge * e, uint32_t i, float * nx, float * ny) {
  float x0, y0, x1, y1;
  cutgraph_polypt(g, e, i > 0 ? i - 1 : 0, &x0, &y0);
  cutgraph_polypt(g, e, i <= e->npts ? i + 1 : i, &x1, &y1);
  float dx = x1 - x0, dy = y1 - y0, l = hypotf(dx, dy);
  *nx = l > 0 ? -dy / l : 0;
  *ny = l > 0 ? dx / l : 0;
}

// Point and smooth unit normal at s along a polyline edge (normals blend along each step)
static void cutgraph_polyat(const CutGraph * g, const CutEdge * e, float s, float * px, float * py, float * qx, float * qy) {
  float acc = 0;
  *px = g->v[e->a].x; *py = g->v[e->a].y; *qx = *qy = 0;
  for(uint32_t i = 0; i <= e->npts; i++) {
    float x0, y0, x1, y1;
    cutgraph_polypt(g, e, i, &x0, &y0);
    cutgraph_polypt(g, e, i + 1, &x1, &y1);
    float l = hypotf(x1 - x0, y1 - y0);
    if(acc + l >= s || i == e->npts) {
      float f = l > 0 ? (s - acc) / l : 0;
      if(f < 0) f = 0;
      if(f > 1) f = 1;
      float n0x, n0y, n1x, n1y;
      cutgraph_polynormal(g, e, i, &n0x, &n0y);
      cutgraph_polynormal(g, e, i + 1, &n1x, &n1y);
      float nx = n0x + (n1x - n0x) * f, ny = n0y + (n1y - n0y) * f, nl = hypotf(nx, ny);
      *px = x0 + (x1 - x0) * f;
      *py = y0 + (y1 - y0) * f;
      *qx = nl > 0 ? nx / nl : 0;
      *qy = nl > 0 ? ny / nl : 0;
      return;
    }
    acc += l;
  }
}

// Polyline edge points with tabs (a included, b excluded): the polyline's own points,
// with tabs replacing the stretches they cover (drawn along the line's smoothed normal)
static uint32_t cutgraph_polypoints(const CutGraph * g, const CutEdge * e, float * xy) {
  uint32_t n = 0, i = 1;
  float acc = 0, px, py, qx, qy;
  xy[n * 2] = g->v[e->a].x; xy[n * 2 + 1] = g->v[e->a].y; n++;
  float pts[KNOB_MAXPOINTS * 2];
  // acc = distance along the line to point i
  float x0 = g->v[e->a].x, y0 = g->v[e->a].y;
  for(int k = 0; k < e->ntabs; k++) {
    const CutTab * t = e->tab + k;
    float s0 = t->pos - t->len / 2, s1 = t->pos + t->len / 2;
    for(; i <= e->npts; i++) {
      float x1, y1;
      cutgraph_polypt(g, e, i, &x1, &y1);
      float d = acc + hypotf(x1 - x0, y1 - y0);
      if(d >= s0) break;
      acc = d;
      x0 = x1; y0 = y1;
      xy[n * 2] = x1; xy[n * 2 + 1] = y1; n++;
    }
    int m = knob_curve(t->kind, &t->tab, pts);
    for(int j = 0; j < m; j++) {
      float o = pts[j * 2 + 1] * t->depth;
      cutgraph_polyat(g, e, s0 + pts[j * 2] * t->len, &px, &py, &qx, &qy);
      xy[n * 2] = px + o * qx; xy[n * 2 + 1] = py + o * qy; n++;
    }
    // Skip the points the tab covers
    for(; i <= e->npts; i++) {
      float x1, y1;
      cutgraph_polypt(g, e, i, &x1, &y1);
      float d = acc + hypotf(x1 - x0, y1 - y0);
      if(d > s1) break;
      acc = d;
      x0 = x1; y0 = y1;
    }
  }
  for(; i <= e->npts; i++) {
    cutgraph_polypt(g, e, i, &px, &py);
    xy[n * 2] = px; xy[n * 2 + 1] = py; n++;
  }
  return n;
}

// Canonical (a -> b) points of an edge in cell units, a included, b excluded
static uint32_t cutgraph_edgepoints(const CutGraph * g, const CutEdge * e, int tabs, float * xy) {
  float ax = g->v[e->a].x, ay = g->v[e->a].y;
  uint32_t n = 0;
  xy[n * 2] = ax; xy[n * 2 + 1] = ay; n++;
  if(e->npts) {
    if(tabs && e->ntabs) return cutgraph_polypoints(g, e, xy);
    memcpy(xy + 2, g->pts + 2 * e->p0, sizeof(float) * 2 * e->npts);
    return 1 + e->npts;
  }
  if(cutgraph_waved(e)) return cutgraph_wavepoints(g, e, tabs, xy);
  if(!tabs || !e->ntabs) return n;
  float dx = g->v[e->b].x - ax, dy = g->v[e->b].y - ay;
  float L = sqrtf(dx * dx + dy * dy);
  float ux = dx / L, uy = dy / L, nx = -uy, ny = ux;
  float pts[KNOB_MAXPOINTS * 2];
  for(int k = 0; k < e->ntabs; k++) {
    const CutTab * t = e->tab + k;
    int m = knob_curve(t->kind, &t->tab, pts);
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
  return 1 + e->npts + e->ntabs * KNOB_MAXPOINTS + (cutgraph_waved(e) ? CUTGRAPH_WAVESEG : 0);
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
  float stackbuf[(2 * KNOB_MAXPOINTS + 1) * 2];
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

static int cutgraph_givetabs(CutGraph * g, const CutTabStyle * st, uint32_t seed, const uint8_t * wv);

int cutgraph_tabs(CutGraph * g, const CutTabStyle * st, uint32_t seed) {
  uint8_t * wv = NULL;
  if(st->whimsytab > 0 && !(wv = cutgraph_whimsyverts(g))) return 1;
  int result = cutgraph_givetabs(g, st, seed, wv);
  free(wv);
  return result;
}

static int cutgraph_givetabs(CutGraph * g, const CutTabStyle * st, uint32_t seed, const uint8_t * wv) {
  Rng r;
  rng_init(&r, seed ^ 0x7AB5u);
  for(uint32_t i = 0; i < g->ne; i++) {
    CutEdge * e = g->e + i;
    if(e->flags & (CUTEDGE_GRID | CUTEDGE_WHIMSY)) continue;
    float L = cutgraph_arclen(g, e);
    e->ntabs = !cutgraph_tabbable(g, st, e, st->mintab, wv) ? 0 : L >= st->twotab ? 2 : 1;
    cutgraph_rolltabs(g, st, e, &r);
  }
  // Every piece but a whimsy needs a tab or blank: give tabless pieces one on their longest
  // inner edge
  for(uint32_t p = 0; p < g->count; p++) {
    if(cutgraph_piecetabs(g, p) || cutgraph_iswhimsy(g, p) || cutgraph_snaked(g, p)) continue;
    int32_t best = -1;
    float bestlen = 0;
    for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
      CutEdge * e = g->e + (g->pedge[k] >> 1);
      if(!cutgraph_tabbable(g, st, e, st->fallbacktab > 0 ? st->fallbacktab : st->mintab * 0.6f, wv)) continue;
      float L = cutgraph_arclen(g, e);
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
      // Whimsies and the pieces around them have their own checks (a star's tips are meant
      // to be sharp, and so are the corners where a whimsy meets a cut). The thickness check
      // goes first: it's cheaper and catches most bad pieces.
      // Swirls need a little less (their fingers are as thin as they can be at their size)
      float cl = st->clearance;
      if(cl > CUTGRAPH_SWIRL_CLEAR) {
        for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
          const CutEdge * ek = g->e + (g->pedge[k] >> 1);
          if(ek->ntabs && ek->tab[0].kind == CUTKNOB_SWIRL) cl = CUTGRAPH_SWIRL_CLEAR;
        }
      }
      int good = (st->clearance <= 0 || cutgraph_touchwhimsy(g, p) || cutgraph_thick_ok(xy, n, cl)) &&
          edgegen_shape_ok(xy, n, CUTGRAPH_CLEARANCE);
      if(good) continue;
      any = 1;
      // Rerolling isn't working: give up a tab on the shortest tabbed edge, as long as
      // both pieces keep one (pieces next to whimsies can be too cramped for all of them)
      // A piece that keeps failing first trades its curls for plain knobs
      int hadcurl = 0;
      if(pass >= CUTGRAPH_CURLFALLBACK) {
        for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
          CutEdge * e = g->e + (g->pedge[k] >> 1);
          for(int j = 0; j < e->ntabs; j++) hadcurl |= e->tab[j].kind == CUTKNOB_CURL || e->tab[j].kind == CUTKNOB_SWIRL;
          if(e->ntabs && (e->tab[0].kind == CUTKNOB_CURL || e->tab[0].kind == CUTKNOB_SWIRL)) e->flags |= CUTEDGE_NOCURL;
        }
      }
      if(pass >= CUTGRAPH_FALLBACK && !hadcurl) {
        int32_t drop = -1;
        float droplen = 1e30f;
        for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
          CutEdge * e = g->e + (g->pedge[k] >> 1);
          if(!e->ntabs) continue;
          int keepl = !cutgraph_snaked(g, e->left), keepr = !cutgraph_snaked(g, e->right);
          if((keepl && cutgraph_piecetabs(g, e->left) <= e->ntabs) ||
              (keepr && cutgraph_piecetabs(g, e->right) <= e->ntabs)) continue;
          float L = cutgraph_arclen(g, e);
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
  for(uint32_t i = 0; i < g->ne; i++) {
    // + the line samples of waved edges, on each side
    const CutEdge * e = g->e + i;
    if(cutgraph_waved(e)) nhb += (CUTGRAPH_WAVEBODY - 1) * ((e->left >= 0) + (e->right >= 0));
  }
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

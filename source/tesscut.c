#include "tesscut.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TESS_SQRT3 1.7320508f
#define TESS_SUBDIV 1       // Each shape segment split in this many (1: segments stay straight under bends)
#define TESS_MAXPASSES 24   // Reroll passes for thin pieces
#define TESS_EPS 1e-3f      // Curve match between a tile and its neighbour (cells)

// Lizard: traced (notes/tools/tess/tess_trace.c) from a teacher's Escher-inspired example in
// https://www.livescience.com/50027-tessellation-tiling.html (its corners are a regular
// hexagon within 2 degrees). Each free edge is averaged with its turned partner, simplified,
// and its corners sharpened (lines fitted per segment, crossed; cut-off corners collapsed).
// Free edge 0 (traced corners 0 -> 1), tolerance 0.0023
static const float tess_liz0[] = { 0.017f, 0.006f, 0.150f, 0.137f, 0.262f, 0.238f, 0.325f, 0.245f, 0.456f, 0.242f, 0.548f, -0.198f, 0.607f, -0.222f, 0.853f, -0.279f, 0.899f, -0.226f, 0.938f, -0.146f };
// Free edge 1 (traced corners 2 -> 3), tolerance 0.0034
static const float tess_liz2[] = { 0.065f, 0.142f, 0.153f, 0.275f, 0.322f, 0.514f, 0.405f, 0.612f, 0.436f, 0.567f, 0.444f, 0.489f, 0.406f, 0.032f, 0.430f, -0.023f, 0.627f, -0.249f, 0.672f, -0.276f, 0.721f, -0.285f, 0.871f, -0.258f, 0.972f, -0.226f };
// Free edge 2 (traced corners 4 -> 5), tolerance 0.0034
static const float tess_liz4[] = { 0.010f, 0.004f, 0.063f, 0.247f, 0.130f, 0.251f, 0.249f, 0.237f, 0.282f, 0.187f, 0.267f, 0.043f, 0.283f, -0.026f, 0.591f, -0.500f, 0.665f, -0.483f };
static const TessShape tess_lizard = { {
  { sizeof tess_liz0 / sizeof(float) / 2, tess_liz0 },
  { sizeof tess_liz2 / sizeof(float) / 2, tess_liz2 },
  { sizeof tess_liz4 / sizeof(float) / 2, tess_liz4 },
} };

const TessShape * tesscut_lizard(void) {
  return &tess_lizard;
}

static void tess_rot(float ang, float x, float y, float * ox, float * oy) {
  float c = cosf(ang), s = sinf(ang);
  *ox = x * c - y * s;
  *oy = x * s + y * c;
}

static void tess_vertex(int k, float * x, float * y) {
  float a = (float)M_PI / 3 * (k % 6);
  *x = cosf(a);
  *y = sinf(a);
}

// Points along local edge k of the unit tile, ends included (count: tess_edgepoints)
static uint32_t tess_edgepoints(const TessShape * sh, int k) {
  return (sh->c[k / 2].n + 1) * TESS_SUBDIV + 1;
}

static void tess_edge(const TessShape * sh, int k, float * xy) {
  int f = k & ~1;   // The free edge it comes from
  const TessCurve * c = &sh->c[f / 2];
  float ax, ay, bx, by;
  tess_vertex(f, &ax, &ay);
  tess_vertex(f + 1, &bx, &by);
  float dx = bx - ax, dy = by - ay;   // Unit length (unit hexagon side)
  float nx = dy, ny = -dx;            // Outward: the hexagon goes clockwise on screen
  // Shape points (ends + curve), then each segment split
  uint32_t ns = c->n + 2, m = 0;
  float sp[2 * 64];
  sp[0] = ax; sp[1] = ay;
  for(uint32_t i = 0; i < c->n && i < 60; i++) {
    float t = c->tn[2 * i], n = c->tn[2 * i + 1];
    sp[2 * (i + 1)] = ax + dx * t + nx * n;
    sp[2 * (i + 1) + 1] = ay + dy * t + ny * n;
  }
  sp[2 * (ns - 1)] = bx; sp[2 * (ns - 1) + 1] = by;
  for(uint32_t i = 0; i + 1 < ns; i++) {
    for(int s = 0; s < TESS_SUBDIV; s++) {
      float u = (float)s / TESS_SUBDIV;
      xy[2 * m] = sp[2 * i] + (sp[2 * i + 2] - sp[2 * i]) * u;
      xy[2 * m + 1] = sp[2 * i + 1] + (sp[2 * i + 3] - sp[2 * i + 1]) * u;
      m++;
    }
  }
  xy[2 * m] = bx; xy[2 * m + 1] = by;
  m++;
  if(!(k & 1)) return;
  // Partner: the free edge reversed, turned -120 degrees around V_k (its end)
  float px, py;
  tess_vertex(k, &px, &py);
  float tmp[2 * 64 * TESS_SUBDIV];
  memcpy(tmp, xy, sizeof(float) * 2 * m);
  for(uint32_t i = 0; i < m; i++) {
    const float * q = tmp + 2 * (m - 1 - i);
    float rx, ry;
    tess_rot(-2 * (float)M_PI / 3, q[0] - px, q[1] - py, &rx, &ry);
    xy[2 * i] = px + rx;
    xy[2 * i + 1] = py + ry;
  }
}

uint32_t tesscut_tilepoints(const TessShape * sh) {
  uint32_t n = 0;
  for(int k = 0; k < 6; k++) n += tess_edgepoints(sh, k) - 1;
  return n;
}

uint32_t tesscut_tile(const TessShape * sh, float * xy) {
  uint32_t n = 0;
  for(int k = 0; k < 6; k++) {
    tess_edge(sh, k, xy + 2 * n);
    n += tess_edgepoints(sh, k) - 1;   // Last point = next edge's first
  }
  return n;
}

// Working state: each edge's unbent points (after vertex jitter) and its current bend
typedef struct {
  float * base;       // 2 * npts per edge at the edge's p0
  float * bend;       // 2 per edge
} TessWork;

static void tess_applybend(CutGraph * g, const TessWork * w, uint32_t ei) {
  CutEdge * e = g->e + ei;
  const float * b = w->base + 2 * e->p0;
  float ax = g->v[e->a].x, ay = g->v[e->a].y, bx = g->v[e->b].x, by = g->v[e->b].y;
  float dx = bx - ax, dy = by - ay, L = sqrtf(dx * dx + dy * dy);
  if(L <= 0) return;
  float nx = -dy / L, ny = dx / L;
  for(uint32_t i = 0; i < e->npts; i++) {
    float t = (float)(i + 1) / (e->npts + 1);
    float d = L * (w->bend[2 * ei] * sinf((float)M_PI * t) + w->bend[2 * ei + 1] * sinf(2 * (float)M_PI * t));
    g->pts[2 * (e->p0 + i)] = b[2 * i] + nx * d;
    g->pts[2 * (e->p0 + i) + 1] = b[2 * i + 1] + ny * d;
  }
}

int tesscut_graph(uint16_t cols, uint16_t rows, const TessShape * sh, const TessVary * v, uint32_t seed,
    CutGraph * g, uint8_t ** orient, TessStats * stats) {
  memset(g, 0, sizeof(CutGraph));
  memset(stats, 0, sizeof(TessStats));
  if(orient) *orient = NULL;
  // Hexagon radius for one cell of area per tile; lattice like hexcut (x in thirds of the
  // column spacing, y in half heights)
  float s = sqrtf(2 / (3 * TESS_SQRT3)), ux = s / 2, uy = s * TESS_SQRT3 / 2;
  int nc = (int)ceilf(cols / (1.5f * s)) + 1, nr = (int)ceilf(rows / (TESS_SQRT3 * s)) + 1;
  int lx0 = -2, ly0 = -1, lw = 3 * (nc - 1) + 5, lh = 2 * nr + 3;
  uint32_t nl = (uint32_t)lw * lh;
  // Local edges of the unit tile
  uint32_t ept[6], maxept = 0;
  float * local[6] = { 0 };
  for(int k = 0; k < 6; k++) {
    ept[k] = tess_edgepoints(sh, k);
    if(ept[k] > maxept) maxept = ept[k];
    local[k] = malloc(sizeof(float) * 2 * ept[k]);
    if(local[k]) tess_edge(sh, k, local[k]);
  }
  g->count = (uint32_t)nc * nr;
  g->width = cols;
  g->height = rows;
  uint32_t maxe = g->count * 6;
  int32_t * vid = malloc(sizeof(int32_t) * nl);
  uint32_t * vedge = malloc(sizeof(uint32_t) * 3 * nl);
  uint8_t * vdeg = calloc(nl, 1);
  g->v = malloc(sizeof(CutVert) * nl);
  g->e = calloc(maxe, sizeof(CutEdge));
  g->pts = malloc(sizeof(float) * 2 * maxe * maxept);
  uint8_t * ori = malloc(g->count);
  float * world = malloc(sizeof(float) * 2 * maxept);
  TessWork w = { malloc(sizeof(float) * 2 * maxe * maxept), calloc(2 * maxe, sizeof(float)) };
  float * xy = NULL;
  int ok = vid && vedge && vdeg && g->v && g->e && g->pts && ori && world && w.base && w.bend;
  for(int k = 0; k < 6; k++) ok &= local[k] != NULL;
  if(!ok) goto fail;
  for(uint32_t i = 0; i < nl; i++) vid[i] = -1;
  // Whole tiles, column by column; tile orientation r = (center lattice y) mod 3 makes the
  // turned edges meet (across an even world edge r goes up one, across an odd one down one)
  uint32_t p = 0;
  for(int i = 0; i < nc; i++) {
    for(int j = 0; j < nr; j++, p++) {
      int cx = 3 * i, cy = 2 * j + (i & 1), r = cy % 3;
      ori[p] = r;
      static const int cdx[6] = { 2, 1, -1, -2, -1, 1 }, cdy[6] = { 0, 1, 1, 0, -1, -1 };
      for(int jw = 0; jw < 6; jw++) {
        int k = ((jw - 2 * r) % 6 + 6) % 6;
        uint32_t l[2];
        for(int e = 0; e < 2; e++) {
          int lx = cx + cdx[(jw + e) % 6] - lx0, ly = cy + cdy[(jw + e) % 6] - ly0;
          l[e] = (uint32_t)ly * lw + lx;
          if(vid[l[e]] < 0) {
            vid[l[e]] = (int32_t)g->nv;
            g->v[g->nv++] = (CutVert) { (lx + lx0) * ux, (ly + ly0) * uy };
          }
        }
        // The tile's edge in the world: turned 120 r degrees, scaled, moved to the center
        for(uint32_t q = 0; q < ept[k]; q++) {
          float rx, ry;
          tess_rot(2 * (float)M_PI / 3 * r, local[k][2 * q], local[k][2 * q + 1], &rx, &ry);
          world[2 * q] = cx * ux + s * rx;
          world[2 * q + 1] = cy * uy + s * ry;
        }
        uint32_t a = vid[l[0]], b = vid[l[1]];
        int fwd = a < b;
        if(!fwd) { uint32_t t = a; a = b; b = t; }
        uint32_t la = fwd ? l[0] : l[1], lb = fwd ? l[1] : l[0], n = ept[k] - 2;
        CutEdge * found = NULL;
        for(int d = 0; d < vdeg[la]; d++) {
          CutEdge * c = g->e + vedge[3 * la + d];
          if(c->b == b) found = c;
        }
        if(!found) {
          if(g->ne >= maxe || vdeg[la] >= 3 || vdeg[lb] >= 3) goto fail;
          found = g->e + g->ne;
          *found = (CutEdge) { .a = a, .b = b, .left = -1, .right = -1, .p0 = g->npts, .npts = n };
          for(uint32_t q = 0; q < n; q++) {
            const float * src = world + 2 * (fwd ? q + 1 : n - q);
            g->pts[2 * (g->npts + q)] = src[0];
            g->pts[2 * (g->npts + q) + 1] = src[1];
          }
          g->npts += n;
          vedge[3 * la + vdeg[la]++] = g->ne;
          vedge[3 * lb + vdeg[lb]++] = g->ne;
          g->ne++;
        } else {
          // Seen from the other tile: the same curve?
          int same = found->npts == n;
          for(uint32_t q = 0; q < n && same; q++) {
            const float * src = world + 2 * (fwd ? q + 1 : n - q), * have = g->pts + 2 * (found->p0 + q);
            same = fabsf(src[0] - have[0]) < TESS_EPS && fabsf(src[1] - have[1]) < TESS_EPS;
          }
          stats->mismatched += !same;
        }
        if(fwd) found->left = (int32_t)p;
        else found->right = (int32_t)p;
      }
    }
  }
  if(cutgraph_rewalk(g)) goto fail;
  xy = malloc(sizeof(float) * 2 * (cutgraph_maxpoints(g) + 8));
  if(!xy) goto fail;
  Rng rng;
  rng_init(&rng, seed ^ 0x7E55u);
  for(uint32_t pc = 0; pc < g->count; pc++) {
    uint32_t n = cutgraph_outline(g, pc, 1, 0, 0, xy);
    stats->thinbase += !cutgraph_thick_ok(xy, n, v->clear);
  }
  // Vertex jitter: every edge's points follow its ends (the similarity taking the old ends
  // to the new)
  float * old = malloc(sizeof(CutVert) * g->nv);
  if(!old) goto fail;
  memcpy(old, g->v, sizeof(CutVert) * g->nv);
  for(uint32_t i = 0; i < g->nv; i++) {
    float a = rng_range(&rng, 0, 2 * (float)M_PI), d = s * v->jitter * sqrtf(rng_range(&rng, 0, 1));
    g->v[i].x += d * cosf(a);
    g->v[i].y += d * sinf(a);
  }
  for(uint32_t i = 0; i < g->ne; i++) {
    CutEdge * e = g->e + i;
    float ax = old[2 * e->a], ay = old[2 * e->a + 1], bx = old[2 * e->b], by = old[2 * e->b + 1];
    float dx = bx - ax, dy = by - ay, dd = dx * dx + dy * dy;
    float ex = g->v[e->b].x - g->v[e->a].x, ey = g->v[e->b].y - g->v[e->a].y;
    float cr = (ex * dx + ey * dy) / dd, ci = (ey * dx - ex * dy) / dd;   // (e) / (d), complex
    for(uint32_t q = 0; q < e->npts; q++) {
      float * pt = g->pts + 2 * (e->p0 + q);
      float px = pt[0] - ax, py = pt[1] - ay;
      pt[0] = g->v[e->a].x + px * cr - py * ci;
      pt[1] = g->v[e->a].y + px * ci + py * cr;
    }
  }
  free(old);
  memcpy(w.base, g->pts, sizeof(float) * 2 * g->npts);
  // Bends, rerolled (smaller each pass) around pieces that come out too thin
  for(uint32_t i = 0; i < g->ne; i++) {
    w.bend[2 * i] = rng_range(&rng, -v->bump, v->bump);
    w.bend[2 * i + 1] = rng_range(&rng, -v->bump, v->bump) * 0.5f;
    tess_applybend(g, &w, i);
  }
  for(uint32_t pass = 0; pass <= TESS_MAXPASSES; pass++) {
    int bad = 0;
    float amp = v->bump * (1 - (float)pass / TESS_MAXPASSES);
    for(uint32_t pc = 0; pc < g->count; pc++) {
      uint32_t n = cutgraph_outline(g, pc, 1, 0, 0, xy);
      if(cutgraph_thick_ok(xy, n, v->clear)) continue;
      bad++;
      for(uint32_t q = g->pstart[pc]; q < g->pstart[pc + 1]; q++) {
        uint32_t ei = g->pedge[q] / 2;
        w.bend[2 * ei] = rng_range(&rng, -amp, amp);
        w.bend[2 * ei + 1] = rng_range(&rng, -amp, amp) * 0.5f;
        stats->flattened += pass == TESS_MAXPASSES;
        tess_applybend(g, &w, ei);
      }
    }
    stats->passes = pass;
    if(!bad) break;
  }
  free(xy);
  free(vid); free(vedge); free(vdeg); free(world); free(w.base); free(w.bend);
  for(int k = 0; k < 6; k++) free(local[k]);
  if(orient) *orient = ori;
  else free(ori);
  return 0;
fail:
  free(xy);
  free(vid); free(vedge); free(vdeg); free(world); free(w.base); free(w.bend); free(ori);
  for(int k = 0; k < 6; k++) free(local[k]);
  cutgraph_free(g);
  return 1;
}

#include "tesscut.h"
#include "rng.h"
#include "geom.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TESS_SQRT3 1.7320508f
#define TESS_SUBDIV 1       // Each shape segment split in this many (1: segments stay straight under bends)
#define TESS_MAXPASSES 24   // Reroll passes for thin pieces
#define TESS_EPS 1e-3f      // Curve match between a tile and its neighbour (cells)
#define TESS_FITS 4         // Builds to fit the piece count (tile size rescaled each time)
#define TESS_MAXFAILS 8     // Failed builds (retried with the lattice moved) before giving up
#define TESS_MAXMERGE 32    // Merge rounds for small / thin border bits
#define TESS_MAXDEG 4       // Edges at a lattice vertex (hex 3, diamond 4)
#define TESS_BORDERGAP 0.02f  // Points this close to a border line are pushed off it (cells), so
                              // crossings there are well apart and come out in the right order

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
// Flower (SHELVED 2026-11-01: too close to the lizard; not in the menu): a whole flower per
// piece. Every edge is the same S, symmetric about its middle (a petal out in the first half,
// the neighbour's petal's notch in the second), which makes the six turned copies around a
// tile all alike: six petals, pinwheel twist, every tile the same
static const float tess_flower0[] = { 0.04f, 0.06f, 0.09f, 0.17f, 0.16f, 0.27f, 0.25f, 0.33f, 0.33f, 0.33f, 0.4f, 0.27f, 0.45f, 0.16f, 0.48f, 0.06f, 0.52f, -0.06f, 0.55f, -0.16f, 0.6f, -0.27f, 0.67f, -0.33f, 0.75f, -0.33f, 0.84f, -0.27f, 0.91f, -0.17f, 0.96f, -0.06f };
static const TessShape tess_flower = { {
  { sizeof tess_flower0 / sizeof(float) / 2, tess_flower0 },
  { sizeof tess_flower0 / sizeof(float) / 2, tess_flower0 },
  { sizeof tess_flower0 / sizeof(float) / 2, tess_flower0 },
}, TESS_LATTICE_HEX };
static const TessShape tess_lizard = { {
  { sizeof tess_liz0 / sizeof(float) / 2, tess_liz0 },
  { sizeof tess_liz2 / sizeof(float) / 2, tess_liz2 },
  { sizeof tess_liz4 / sizeof(float) / 2, tess_liz4 },
}, TESS_LATTICE_HEX };

// Pinwheel: from a quilt (notes/tools/quilt/: a pinwheel star of 18 grid squares, a 4 x 4 middle
// with its corners cut on the diagonal and one square arm per side, set on a 45 degree grid).
// Its side between two corners is this sawtooth, point-symmetric about its middle.
static const float tess_pin0[] = { 1 / 6.0f, -1 / 6.0f, 0.5f, 1 / 6.0f, 0.5f, -1 / 6.0f, 5 / 6.0f, 1 / 6.0f };
static const TessShape tess_pinwheel = { { { sizeof tess_pin0 / sizeof(float) / 2, tess_pin0 } },
    TESS_LATTICE_DIAMOND };

const TessShape * tesscut_lizard(void) {
  return &tess_lizard;
}

const TessShape * tesscut_shape(uint8_t shape) {
  return shape == TESSCUT_SHAPE_FLOWER ? &tess_flower : shape == TESSCUT_SHAPE_PINWHEEL ? &tess_pinwheel : &tess_lizard;
}

static void tess_rot(float ang, float x, float y, float * ox, float * oy) {
  float c = cosf(ang), s = sinf(ang);
  *ox = x * c - y * s;
  *oy = x * s + y * c;
}

static int tess_nedges(const TessShape * sh) {
  return sh->lattice == TESS_LATTICE_DIAMOND ? 4 : 6;
}

static void tess_vertex(const TessShape * sh, int k, float * x, float * y) {
  int n = tess_nedges(sh);
  float a = 2 * (float)M_PI / n * (k % n) - (n == 4 ? (float)M_PI / 2 : 0);
  *x = cosf(a);
  *y = sinf(a);
}

// The free curve local edge k comes from
static const TessCurve * tess_curve(const TessShape * sh, int k) {
  return sh->lattice == TESS_LATTICE_DIAMOND ? &sh->c[0] : &sh->c[k / 2];
}

// Points along local edge k of the unit tile, ends included (count: tess_edgepoints)
static uint32_t tess_edgepoints(const TessShape * sh, int k) {
  return (tess_curve(sh, k)->n + 1) * TESS_SUBDIV + 1;
}

static void tess_edge(const TessShape * sh, int k, float * xy) {
  int diamond = sh->lattice == TESS_LATTICE_DIAMOND;
  int f = diamond ? 0 : k & ~1;   // The free edge it comes from
  const TessCurve * c = tess_curve(sh, k);
  float ax, ay, bx, by;
  tess_vertex(sh, f, &ax, &ay);
  tess_vertex(sh, f + 1, &bx, &by);
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
  if(diamond) {
    // Edge 0 turned 90 k degrees around the center
    for(uint32_t i = 0; i < m; i++) tess_rot((float)M_PI / 2 * k, xy[2 * i], xy[2 * i + 1], xy + 2 * i, xy + 2 * i + 1);
    return;
  }
  if(!(k & 1)) return;
  // Partner: the free edge reversed, turned -120 degrees around V_k (its end)
  float px, py;
  tess_vertex(sh, k, &px, &py);
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
  for(int k = 0; k < tess_nedges(sh); k++) n += tess_edgepoints(sh, k) - 1;
  return n;
}

uint32_t tesscut_tile(const TessShape * sh, float * xy) {
  uint32_t n = 0;
  for(int k = 0; k < tess_nedges(sh); k++) {
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

static float tess_arclen(const CutGraph * g, const CutEdge * e) {
  float L = 0, px = g->v[e->a].x, py = g->v[e->a].y;
  for(uint32_t i = 0; i <= e->npts; i++) {
    float qx = i < e->npts ? g->pts[2 * (e->p0 + i)] : g->v[e->b].x;
    float qy = i < e->npts ? g->pts[2 * (e->p0 + i) + 1] : g->v[e->b].y;
    L += hypotf(qx - px, qy - py);
    px = qx; py = qy;
  }
  return L;
}

// Lattice building: vertices by lattice point; an edge seen again from the other tile is checked
typedef struct {
  CutGraph * g;
  int32_t * vid;      // Per lattice point: vertex, or -1
  uint32_t * vedge;   // TESS_MAXDEG per lattice point
  uint8_t * vdeg;
  uint32_t maxe;
  TessStats * stats;
} TessBuild;

static void tess_vid(TessBuild * b, uint32_t l, float x, float y) {
  if(b->vid[l] >= 0) return;
  b->vid[l] = (int32_t)b->g->nv;
  b->g->v[b->g->nv++] = (CutVert) { x, y };
}

// Tile p's edge from lattice point l0 to l1: world = its points, ends included (n + 2). Added,
// or compared with the neighbour's copy. Returns 0 on success.
static int tess_addedge(TessBuild * bd, uint32_t l0, uint32_t l1, const float * world, uint32_t n, uint32_t p) {
  CutGraph * g = bd->g;
  uint32_t a = bd->vid[l0], b = bd->vid[l1];
  int fwd = a < b;
  if(!fwd) { uint32_t t = a; a = b; b = t; }
  uint32_t la = fwd ? l0 : l1, lb = fwd ? l1 : l0;
  CutEdge * found = NULL;
  for(int d = 0; d < bd->vdeg[la]; d++) {
    CutEdge * c = g->e + bd->vedge[TESS_MAXDEG * la + d];
    if(c->b == b) found = c;
  }
  if(!found) {
    if(g->ne >= bd->maxe || bd->vdeg[la] >= TESS_MAXDEG || bd->vdeg[lb] >= TESS_MAXDEG) return 1;
    found = g->e + g->ne;
    *found = (CutEdge) { .a = a, .b = b, .left = -1, .right = -1, .p0 = g->npts, .npts = n };
    for(uint32_t q = 0; q < n; q++) {
      const float * src = world + 2 * (fwd ? q + 1 : n - q);
      g->pts[2 * (g->npts + q)] = src[0];
      g->pts[2 * (g->npts + q) + 1] = src[1];
    }
    g->npts += n;
    bd->vedge[TESS_MAXDEG * la + bd->vdeg[la]++] = g->ne;
    bd->vedge[TESS_MAXDEG * lb + bd->vdeg[lb]++] = g->ne;
    g->ne++;
  } else {
    // Seen from the other tile: the same curve?
    int same = found->npts == n;
    for(uint32_t q = 0; q < n && same; q++) {
      const float * src = world + 2 * (fwd ? q + 1 : n - q), * have = g->pts + 2 * (found->p0 + q);
      same = fabsf(src[0] - have[0]) < TESS_EPS && fabsf(src[1] - have[1]) < TESS_EPS;
    }
    bd->stats->mismatched += !same;
  }
  if(fwd) found->left = (int32_t)p;
  else found->right = (int32_t)p;
  return 0;
}

// Whole tiles of radius s over (0,0)-(w,h) and a tile beyond, lattice origin at (ox, oy)
// (<= -s), varied (jitter, bends rerolled around thin pieces). ori: per tile.
// Hex: lattice points (ox + x s/2, oy + y s sqrt3/2), centers every 3 x and 2 y (odd columns a
// row down). Diamond: lattice points (ox + x s, oy + y s), centers where x + y is even.
static int tess_tiles(float w, float h, float s, float ox, float oy, const TessShape * sh, const TessVary * v,
    uint32_t seed, CutGraph * g, uint8_t ** orient, TessStats * stats) {
  memset(g, 0, sizeof(CutGraph));
  int diamond = sh->lattice == TESS_LATTICE_DIAMOND, nedges = tess_nedges(sh);
  float ux = diamond ? s : s / 2, uy = diamond ? s : s * TESS_SQRT3 / 2;
  int nc, nr, lx0, ly0, lw, lh;
  if(diamond) {
    // Centers (x, y) for x in 1..nc, y in 1..nr, x + y even
    nc = (int)ceilf((w - ox) / s) + 1;
    nr = (int)ceilf((h - oy) / s) + 1;
    lx0 = 0; ly0 = 0; lw = nc + 2; lh = nr + 2;
    g->count = 0;
    for(int i = 1; i <= nc; i++) g->count += (nr + (i & 1)) / 2;
  } else {
    nc = (int)ceilf((w - ox + s) / (1.5f * s)) + 1;
    nr = (int)ceilf((h - oy + s) / (TESS_SQRT3 * s)) + 1;
    lx0 = -2; ly0 = -1; lw = 3 * (nc - 1) + 5; lh = 2 * nr + 3;
    g->count = (uint32_t)nc * nr;
  }
  uint32_t nl = (uint32_t)lw * lh;
  uint32_t ept[6], maxept = 0;
  float * local[6] = { 0 };
  for(int k = 0; k < nedges; k++) {
    ept[k] = tess_edgepoints(sh, k);
    if(ept[k] > maxept) maxept = ept[k];
    local[k] = malloc(sizeof(float) * 2 * ept[k]);
    if(local[k]) tess_edge(sh, k, local[k]);
  }
  g->width = w;
  g->height = h;
  uint32_t maxe = g->count * nedges;
  int32_t * vid = malloc(sizeof(int32_t) * nl);
  uint32_t * vedge = malloc(sizeof(uint32_t) * TESS_MAXDEG * nl);
  uint8_t * vdeg = calloc(nl, 1);
  g->v = malloc(sizeof(CutVert) * nl);
  g->e = calloc(maxe, sizeof(CutEdge));
  g->pts = malloc(sizeof(float) * 2 * maxe * maxept);
  uint8_t * ori = malloc(g->count);
  float * world = malloc(sizeof(float) * 2 * maxept);
  TessWork wk = { malloc(sizeof(float) * 2 * maxe * maxept), calloc(2 * maxe, sizeof(float)) };
  float * xy = NULL, * old = NULL;
  TessBuild bd = { g, vid, vedge, vdeg, maxe, stats };
  int ok = vid && vedge && vdeg && g->v && g->e && g->pts && ori && world && wk.base && wk.bend;
  for(int k = 0; k < nedges; k++) ok &= local[k] != NULL;
  if(!ok) goto fail;
  for(uint32_t i = 0; i < nl; i++) vid[i] = -1;
  uint32_t p = 0;
  if(diamond) {
    static const int vdx[4] = { 0, 1, 0, -1 }, vdy[4] = { -1, 0, 1, 0 };   // V0..V3
    for(int cx = 1; cx <= nc; cx++) {
      for(int cy = 2 - (cx & 1); cy <= nr; cy += 2, p++) {
        ori[p] = 0;
        for(int k = 0; k < 4; k++) {
          uint32_t l[2];
          for(int e = 0; e < 2; e++) {
            int lx = cx + vdx[(k + e) % 4], ly = cy + vdy[(k + e) % 4];
            l[e] = (uint32_t)ly * lw + lx;
            tess_vid(&bd, l[e], ox + lx * ux, oy + ly * uy);
          }
          for(uint32_t q = 0; q < ept[k]; q++) {
            world[2 * q] = ox + cx * ux + s * local[k][2 * q];
            world[2 * q + 1] = oy + cy * uy + s * local[k][2 * q + 1];
          }
          if(tess_addedge(&bd, l[0], l[1], world, ept[k] - 2, p)) goto fail;
        }
      }
    }
  } else {
    // Tile orientation r = (center lattice y) mod 3 makes the turned edges meet (across an even
    // world edge r goes up one, across an odd one down one)
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
            tess_vid(&bd, l[e], ox + (lx + lx0) * ux, oy + (ly + ly0) * uy);
          }
          // The tile's edge in the world: turned 120 r degrees, scaled, moved to the center
          for(uint32_t q = 0; q < ept[k]; q++) {
            float rx, ry;
            tess_rot(2 * (float)M_PI / 3 * r, local[k][2 * q], local[k][2 * q + 1], &rx, &ry);
            world[2 * q] = ox + cx * ux + s * rx;
            world[2 * q + 1] = oy + cy * uy + s * ry;
          }
          if(tess_addedge(&bd, l[0], l[1], world, ept[k] - 2, p)) goto fail;
        }
      }
    }
  }
  if(cutgraph_rewalk(g)) goto fail;
  xy = malloc(sizeof(float) * 2 * (cutgraph_maxpoints(g) + 8));
  old = malloc(sizeof(CutVert) * g->nv);
  if(!xy || !old) goto fail;
  Rng rng;
  rng_init(&rng, seed ^ 0x7E55u);
  for(uint32_t pc = 0; pc < g->count; pc++) {
    uint32_t n = cutgraph_outline(g, pc, 1, 0, 0, xy);
    stats->thinbase += !cutgraph_thick_ok(xy, n, v->clear);
  }
  // Vertex jitter: every edge's points follow its ends (the similarity taking the old ends
  // to the new)
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
  memcpy(wk.base, g->pts, sizeof(float) * 2 * g->npts);
  // Bends, rerolled (smaller each pass) around pieces that come out too thin
  for(uint32_t i = 0; i < g->ne; i++) {
    wk.bend[2 * i] = rng_range(&rng, -v->bump, v->bump);
    wk.bend[2 * i + 1] = rng_range(&rng, -v->bump, v->bump) * 0.5f;
    tess_applybend(g, &wk, i);
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
        wk.bend[2 * ei] = rng_range(&rng, -amp, amp);
        wk.bend[2 * ei + 1] = rng_range(&rng, -amp, amp) * 0.5f;
        stats->flattened += pass == TESS_MAXPASSES;
        tess_applybend(g, &wk, ei);
      }
    }
    stats->passes = pass;
    if(!bad) break;
  }
  // Nothing sits (almost) on the border lines
  for(uint32_t i = 0; i < g->nv + g->npts; i++) {
    float * pt = i < g->nv ? &g->v[i].x : g->pts + 2 * (i - g->nv);
    float lines[4] = { 0, w, 0, h };
    for(int k = 0; k < 4; k++) {
      float * c = pt + (k >= 2), d = *c - lines[k];
      if(fabsf(d) < TESS_BORDERGAP) *c = lines[k] + (d < 0 ? -TESS_BORDERGAP : TESS_BORDERGAP);
    }
  }
  free(xy); free(old);
  free(vid); free(vedge); free(vdeg); free(world); free(wk.base); free(wk.bend);
  for(int k = 0; k < 6; k++) free(local[k]);
  *orient = ori;
  return 0;
fail:
  free(xy); free(old);
  free(vid); free(vedge); free(vdeg); free(world); free(wk.base); free(wk.bend); free(ori);
  for(int k = 0; k < 6; k++) free(local[k]);
  cutgraph_free(g);
  return 1;
}

// Where along the rectangle's outline (clockwise from the top left) a point on it sits
static float tess_perim(float x, float y, float w, float h) {
  float dt = fabsf(y), dr = fabsf(x - w), db = fabsf(y - h), dl = fabsf(x);
  float m = fminf(fminf(dt, dr), fminf(db, dl));
  if(m == dt) return x;
  if(m == dr) return w + y;
  if(m == db) return w + h + (w - x);
  return 2 * w + h + (h - y);
}

typedef struct {
  float u;          // tess_perim
  uint32_t v;
} TessBorderPt;

static int tess_bpcmp(const void * a, const void * b) {
  float x = ((const TessBorderPt *)a)->u, y = ((const TessBorderPt *)b)->u;
  return x < y ? -1 : x > y;
}

// Liang-Barsky: part of segment p -> q inside [0,w] x [0,h] as t in [t0, t1]; 0 if none
static int tess_lb(float px, float py, float qx, float qy, float w, float h, float * t0, float * t1) {
  float dx = qx - px, dy = qy - py, P[4] = { -dx, dx, -dy, dy }, Q[4] = { px, w - px, py, h - py };
  float a = 0, b = 1;
  for(int i = 0; i < 4; i++) {
    if(P[i] == 0) {
      if(Q[i] < 0) return 0;
    } else {
      float r = Q[i] / P[i];
      if(P[i] < 0) { if(r > b) return 0; if(r > a) a = r; }
      else { if(r < a) return 0; if(r < b) b = r; }
    }
  }
  *t0 = a; *t1 = b;
  return b - a > 1e-6f;
}

// The tiles cut to the rectangle: edges clipped, border edges added (each belongs to the tile
// it runs through). Pieces keep their tile numbers (a tile may now be in several parts).
static int tess_clip(const CutGraph * g, float w, float h, CutGraph * c) {
  memset(c, 0, sizeof(CutGraph));
  uint32_t nseg = 0;
  for(uint32_t i = 0; i < g->ne; i++) nseg += g->e[i].npts + 1;
  uint32_t maxv = g->nv + 2 * nseg + 4, maxe = nseg + 2 * nseg + 4, maxb = 2 * nseg + 4;
  c->count = g->count;
  c->width = w;
  c->height = h;
  c->v = malloc(sizeof(CutVert) * maxv);
  c->e = calloc(maxe, sizeof(CutEdge));
  c->pts = malloc(sizeof(float) * 2 * (g->npts + 1));
  int32_t * vmap = malloc(sizeof(int32_t) * g->nv);
  TessBorderPt * bp = malloc(sizeof(TessBorderPt) * maxb);
  float * run = malloc(sizeof(float) * 2 * (g->npts + 2));
  int32_t * vedge = malloc(sizeof(int32_t) * maxv);   // Border vertex -> its inner edge
  uint32_t nb = 0;
  int ok = c->v && c->e && c->pts && vmap && bp && run && vedge;
  if(!ok) goto fail;
  for(uint32_t i = 0; i < maxv; i++) vedge[i] = -1;
  for(uint32_t i = 0; i < g->nv; i++) {
    const CutVert * p = g->v + i;
    vmap[i] = p->x >= 0 && p->x <= w && p->y >= 0 && p->y <= h ? (int32_t)c->nv : -1;
    if(vmap[i] >= 0) c->v[c->nv++] = *p;
  }
  for(uint32_t i = 0; i < g->ne; i++) {
    const CutEdge * e = g->e + i;
    uint32_t n = e->npts + 2, nr = 0;
    int32_t rstart = vmap[e->a];
    for(uint32_t k = 0; k + 1 < n; k++) {
      float px, py, qx, qy;
      if(k == 0) { px = g->v[e->a].x; py = g->v[e->a].y; }
      else { px = g->pts[2 * (e->p0 + k - 1)]; py = g->pts[2 * (e->p0 + k - 1) + 1]; }
      if(k + 1 == n - 1) { qx = g->v[e->b].x; qy = g->v[e->b].y; }
      else { qx = g->pts[2 * (e->p0 + k)]; qy = g->pts[2 * (e->p0 + k) + 1]; }
      float t0, t1;
      if(!tess_lb(px, py, qx, qy, w, h, &t0, &t1)) continue;
      if(t0 > 0 || rstart < 0) {   // Comes in here: a new border vertex starts a run
        float x = px + (qx - px) * t0, y = py + (qy - py) * t0;
        rstart = (int32_t)c->nv;
        c->v[c->nv++] = (CutVert) { fminf(fmaxf(x, 0), w), fminf(fmaxf(y, 0), h) };
        bp[nb++] = (TessBorderPt) { tess_perim(x, y, w, h), (uint32_t)rstart };
        nr = 0;
      }
      int32_t rend = -1;
      int last = k + 1 == n - 1;
      if(t1 < 1 || (last && vmap[e->b] < 0)) {   // Goes out here (or ends just outside: rounding)
        float x = px + (qx - px) * t1, y = py + (qy - py) * t1;
        rend = (int32_t)c->nv;
        c->v[c->nv++] = (CutVert) { fminf(fmaxf(x, 0), w), fminf(fmaxf(y, 0), h) };
        bp[nb++] = (TessBorderPt) { tess_perim(x, y, w, h), (uint32_t)rend };
      } else if(last) {
        rend = vmap[e->b];
      } else {
        run[2 * nr] = qx; run[2 * nr + 1] = qy;
        nr++;
        continue;
      }
      // A run ends: an edge from rstart to rend
      if(rstart >= 0 && rend >= 0 && rstart != rend) {
        vedge[rstart] = vedge[rend] = (int32_t)c->ne;   // Only border vertices are looked up
        CutEdge * ne = c->e + c->ne++;
        int fwd = rstart < rend;
        *ne = (CutEdge) { .a = fwd ? rstart : rend, .b = fwd ? rend : rstart, .left = fwd ? e->left : e->right,
            .right = fwd ? e->right : e->left, .p0 = c->npts, .npts = nr };
        for(uint32_t q = 0; q < nr; q++) {
          const float * src = run + 2 * (fwd ? q : nr - 1 - q);
          c->pts[2 * (c->npts + q)] = src[0];
          c->pts[2 * (c->npts + q) + 1] = src[1];
        }
        c->npts += nr;
      }
      rstart = -1;
      nr = 0;
    }
  }
  // Border: corners + crossings in order clockwise. The stretch after a crossing belongs to
  // the piece on the left of its inner edge heading in (left on screen: the right of a walk
  // that goes clockwise); corners carry the owner on
  float cw[4][2] = { { 0, 0 }, { w, 0 }, { w, h }, { 0, h } };
  for(int k = 0; k < 4; k++) {
    bp[nb++] = (TessBorderPt) { tess_perim(cw[k][0], cw[k][1], w, h), c->nv };
    c->v[c->nv++] = (CutVert) { cw[k][0], cw[k][1] };
  }
  qsort(bp, nb, sizeof(TessBorderPt), tess_bpcmp);
  uint32_t first = 0;
  while(first < nb && vedge[bp[first].v] < 0) first++;
  if(first == nb) goto fail;
  int32_t owner = -1;
  for(uint32_t i = 0; i < nb; i++) {
    uint32_t k = (first + i) % nb, va = bp[k].v, vb = bp[(k + 1) % nb].v;
    if(vedge[va] >= 0) {
      const CutEdge * ie = c->e + vedge[va];
      owner = ie->a == va ? ie->right : ie->left;
    }
    if(owner < 0) goto fail;
    CutEdge * ne = c->e + c->ne++;
    int fwd = va < vb;   // The owner walks the border clockwise, like the rectangle
    *ne = (CutEdge) { .a = fwd ? va : vb, .b = fwd ? vb : va, .left = fwd ? owner : -1, .right = fwd ? -1 : owner };
  }
  free(vmap); free(bp); free(run); free(vedge);
  return 0;
fail:
  free(vmap); free(bp); free(run); free(vedge);
  cutgraph_free(c);
  return 1;
}

// A piece in several parts (a tile the border cut through) becomes one piece per part;
// pieces with no edges left go. ori follows. Then the walks are rebuilt.
static int tess_parts(CutGraph * g, uint8_t ** orient) {
  uint32_t count = g->count, total = 0;
  for(uint32_t i = 0; i < g->ne; i++) total += (g->e[i].left >= 0) + (g->e[i].right >= 0);
  uint32_t * start = calloc(count + 1, sizeof(uint32_t)), * fill = malloc(sizeof(uint32_t) * count);
  uint32_t * half = malloc(sizeof(uint32_t) * total);
  int32_t * newid = malloc(sizeof(int32_t) * total);
  uint8_t * ori = malloc(total), * old = *orient;
  int err = !start || !fill || !half || !newid || !ori;
  uint32_t nn = 0;
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
    for(uint32_t i = 0; i < total; i++) newid[i] = -1;
    for(uint32_t p = 0; p < count && !err; p++) {
      uint32_t s = start[p], n = start[p + 1] - s;
      for(uint32_t k0 = 0; k0 < n && !err; k0++) {
        if(newid[s + k0] >= 0) continue;
        // One part: follow half-edges end to start until back
        uint32_t id = nn++, cur = k0;
        ori[id] = old[p];
        for(uint32_t steps = 0; steps <= n; steps++) {
          newid[s + cur] = (int32_t)id;
          uint32_t h = half[s + cur];
          const CutEdge * e = g->e + (h >> 1);
          uint32_t end = (h & 1) ? e->a : e->b;
          int32_t next = -1;
          for(uint32_t j = 0; j < n; j++) {
            uint32_t hj = half[s + j];
            const CutEdge * f = g->e + (hj >> 1);
            if(((hj & 1) ? f->b : f->a) == end && (newid[s + j] < 0 || j == k0)) { next = j; break; }
          }
          if(next < 0) { err = 1; break; }
          if((uint32_t)next == k0) break;
          cur = next;
        }
      }
    }
    for(uint32_t p = 0; p < count && !err; p++) {
      for(uint32_t k = start[p]; k < start[p + 1]; k++) {
        uint32_t h = half[k];
        if(h & 1) g->e[h >> 1].right = newid[k];
        else g->e[h >> 1].left = newid[k];
      }
    }
  }
  free(start); free(fill); free(half); free(newid);
  if(err) { free(ori); return 1; }
  free(old);
  *orient = ori;
  g->count = nn;
  return cutgraph_rewalk(g) != 0;
}

// Border bits that are too small (area < minarea) or too thin (up to 4 x that) join the neighbour they share
// the most edge with; the edges between them go. Pieces renumbered, walks rebuilt.
static int tess_merge(CutGraph * g, uint8_t * ori, float minarea, float clear, TessStats * stats) {
  float * xy = NULL;
  int err = 0;
  for(int round = 0; round < TESS_MAXMERGE && !err; round++) {
    uint32_t count = g->count;
    free(xy);   // Merged pieces have longer outlines: sized again each round
    xy = malloc(sizeof(float) * 2 * (cutgraph_maxpoints(g) + 8));
    if(!xy) { err = 1; break; }
    float * area = malloc(sizeof(float) * count);
    uint8_t * want = calloc(count, 1), * busy = calloc(count, 1);
    int32_t * to = malloc(sizeof(int32_t) * count);
    if(!area || !want || !busy || !to) { free(area); free(want); free(busy); free(to); err = 1; break; }
    for(uint32_t p = 0; p < count; p++) to[p] = -1;   // Set only for the piece that merges away
    for(uint32_t p = 0; p < count; p++) {
      uint32_t n = cutgraph_outline(g, p, 1, 0, 0, xy);
      float cx, cy;
      area[p] = geom_area(xy, n, &cx, &cy);
      int border = 0;
      for(uint32_t q = g->pstart[p]; q < g->pstart[p + 1]; q++) {
        const CutEdge * e = g->e + g->pedge[q] / 2;
        border |= e->left < 0 || e->right < 0;
      }
      want[p] = border && (area[p] < minarea || (area[p] < 4 * minarea && !cutgraph_thick_ok(xy, n, clear)));
    }
    // Smallest first; each piece in at most one merge per round
    int merged = 0;
    for(;;) {
      int32_t f = -1;
      for(uint32_t p = 0; p < count; p++) if(want[p] && !busy[p] && (f < 0 || area[p] < area[f])) f = (int32_t)p;
      if(f < 0) break;
      want[f] = 0;
      // Neighbour with the longest shared edge
      int32_t best = -1;
      float bl = 0;
      for(uint32_t q = g->pstart[f]; q < g->pstart[f + 1]; q++) {
        const CutEdge * e = g->e + g->pedge[q] / 2;
        int32_t o = e->left == f ? e->right : e->left;
        if(o < 0) continue;
        float L = 0;
        for(uint32_t r = g->pstart[f]; r < g->pstart[f + 1]; r++) {
          const CutEdge * e2 = g->e + g->pedge[r] / 2;
          if(e2->left == o || e2->right == o) L += tess_arclen(g, e2);
        }
        if(L > bl && !busy[o]) { bl = L; best = o; }
      }
      if(best < 0) continue;
      busy[f] = busy[best] = 1;
      to[f] = best;
      merged++;
    }
    if(!merged) { free(area); free(want); free(busy); free(to); break; }
    // Relabel merged pieces, drop edges inside a piece, renumber
    uint32_t ne = 0;
    for(uint32_t i = 0; i < g->ne; i++) {
      CutEdge e = g->e[i];
      if(e.left >= 0 && to[e.left] >= 0) e.left = to[e.left];
      if(e.right >= 0 && to[e.right] >= 0) e.right = to[e.right];
      if(e.left == e.right) continue;
      g->e[ne++] = e;
    }
    g->ne = ne;
    stats->merged += merged;
    // Renumber pieces densely
    int32_t * id = malloc(sizeof(int32_t) * count);
    uint8_t * used = calloc(count, 1);
    for(uint32_t i = 0; i < g->ne; i++) {
      if(g->e[i].left >= 0) used[g->e[i].left] = 1;
      if(g->e[i].right >= 0) used[g->e[i].right] = 1;
    }
    uint32_t nn = 0;
    for(uint32_t p = 0; p < count; p++) {
      id[p] = used[p] ? (int32_t)nn : -1;
      if(used[p]) ori[nn++] = ori[p];
    }
    for(uint32_t i = 0; i < g->ne; i++) {
      if(g->e[i].left >= 0) g->e[i].left = id[g->e[i].left];
      if(g->e[i].right >= 0) g->e[i].right = id[g->e[i].right];
    }
    g->count = nn;
    free(id); free(used);
    free(area); free(want); free(busy); free(to);
    if(cutgraph_rewalk(g)) err = 1;
  }
  free(xy);
  return err;
}

// Necks: a border piece whose outline comes closer than neck to itself (across the inside),
// where the smaller side of that gap touches the border (a clipped leg ending in a lump, a
// waist the border left), is cut straight across there and the smaller side joins the
// neighbour it shares the most edge with (not the piece it came from).

// Outline point i of piece p's walk: half-edge and position along it (0 = its start vertex)
typedef struct {
  uint32_t h, k;
} TessOutPt;

static int tess_grow(CutGraph * g, uint32_t nv, uint32_t ne, uint32_t npts) {
  CutVert * v = realloc(g->v, sizeof(CutVert) * (g->nv + nv));
  if(!v) return 1;
  g->v = v;
  CutEdge * e = realloc(g->e, sizeof(CutEdge) * (g->ne + ne));
  if(!e) return 1;
  g->e = e;
  float * p = realloc(g->pts, sizeof(float) * 2 * (g->npts + npts + 1));
  if(!p) return 1;
  g->pts = p;
  return 0;
}

// Edge ei's point m (0 = a, npts + 1 = b)
static void tess_edgept(const CutGraph * g, const CutEdge * e, uint32_t m, float * x, float * y) {
  if(m == 0) { *x = g->v[e->a].x; *y = g->v[e->a].y; }
  else if(m > e->npts) { *x = g->v[e->b].x; *y = g->v[e->b].y; }
  else { *x = g->pts[2 * (e->p0 + m - 1)]; *y = g->pts[2 * (e->p0 + m - 1) + 1]; }
}

// A vertex on edge ei at its segment m (edge order) and t along it: an end vertex if it falls on
// one, else the edge is split there (the second half becomes a new edge, b -> new vertex).
// Returns the vertex, or -1 out of memory.
static int32_t tess_splitat(CutGraph * g, uint32_t ei, uint32_t m, float t) {
  CutEdge * e = g->e + ei;
  uint32_t n = e->npts + 2;
  if(t >= 1 - 1e-4f) { m++; t = 0; }
  if(t <= 1e-4f) {
    t = 0;
    if(m == 0) return (int32_t)e->a;
    if(m == n - 1) return (int32_t)e->b;
  }
  if(tess_grow(g, 1, 1, n)) return -1;
  e = g->e + ei;
  float x0, y0, x1, y1;
  tess_edgept(g, e, m, &x0, &y0);
  tess_edgept(g, e, m + 1 < n ? m + 1 : m, &x1, &y1);
  uint32_t vi = g->nv++;
  g->v[vi] = (CutVert) { x0 + (x1 - x0) * t, y0 + (y1 - y0) * t };
  // Second half: edge points after the split (m + 1 .. n - 2 interior; on a point: m + 1 ..),
  // stored reversed (it runs b -> new vertex, b being the smaller index)
  uint32_t from = m + 1, cnt = n - 2 >= from ? n - 1 - from : 0;
  CutEdge * f = g->e + g->ne++;
  *f = (CutEdge) { .a = e->b, .b = vi, .left = e->right, .right = e->left, .p0 = g->npts, .npts = cnt };
  for(uint32_t q = 0; q < cnt; q++) tess_edgept(g, e, n - 2 - q, g->pts + 2 * (g->npts + q), g->pts + 2 * (g->npts + q) + 1);
  g->npts += cnt;
  // First half: a -> new vertex, interior points 1 .. m (on a point: 1 .. m - 1)
  e->b = vi;
  e->npts = t == 0 ? (m > 0 ? m - 1 : 0) : m;
  return (int32_t)vi;
}

static int tess_isborder(const CutGraph * g, uint32_t p) {
  for(uint32_t q = g->pstart[p]; q < g->pstart[p + 1]; q++) {
    const CutEdge * e = g->e + g->pedge[q] / 2;
    if(e->left < 0 || e->right < 0) return 1;
  }
  return 0;
}

// The vertex at (x, y) on one of piece p's edges: an existing vertex, or one made by splitting
// the edge there. -1 = not found / out of memory.
static int32_t tess_vertexat(CutGraph * g, int32_t p, float x, float y) {
  for(uint32_t i = 0; i < g->nv; i++) {
    if(fabsf(g->v[i].x - x) < 1e-5f && fabsf(g->v[i].y - y) < 1e-5f) return (int32_t)i;
  }
  int32_t bi = -1;
  uint32_t bm = 0;
  float bd = 1e-4f, bt = 0;
  for(uint32_t i = 0; i < g->ne; i++) {
    const CutEdge * e = g->e + i;
    if(e->left != p && e->right != p) continue;
    for(uint32_t m = 0; m <= e->npts; m++) {
      float x0, y0, x1, y1;
      tess_edgept(g, e, m, &x0, &y0);
      tess_edgept(g, e, m + 1, &x1, &y1);
      float dx = x1 - x0, dy = y1 - y0, L2 = dx * dx + dy * dy;
      float t = L2 > 0 ? ((x - x0) * dx + (y - y0) * dy) / L2 : 0;
      t = fminf(fmaxf(t, 0), 1);
      float d = hypotf(x0 + dx * t - x, y0 + dy * t - y);
      if(d < bd) { bd = d; bi = (int32_t)i; bm = m; bt = t; }
    }
  }
  return bi < 0 ? -1 : tess_splitat(g, bi, bm, bt);
}

// Try one neck cut on piece p (a border piece; walks valid). backup gets a copy of g just
// before g is changed. 1 = cut (walks need rebuilding), 0 = none, -1 = error.
static int tess_neckone(CutGraph * g, int32_t p, float neck, CutGraph * backup) {
  uint32_t maxn = cutgraph_maxpoints(g) + 8, n = 0;
  float * xy = malloc(sizeof(float) * 2 * maxn), * arc = malloc(sizeof(float) * (maxn + 1));
  TessOutPt * op = malloc(sizeof(TessOutPt) * maxn);
  float (*cand)[4] = malloc(sizeof(float) * 4 * maxn);   // d, i, j, t
  int result = 0;
  if(!xy || !arc || !op || !cand) { result = -1; goto done; }
  // Outline points with where they come from
  for(uint32_t q = g->pstart[p]; q < g->pstart[p + 1]; q++) {
    uint32_t h = g->pedge[q];
    const CutEdge * e = g->e + h / 2;
    for(uint32_t k = 0; k <= e->npts; k++) {
      uint32_t m = (h & 1) ? e->npts + 1 - k : k;
      tess_edgept(g, e, m, xy + 2 * n, xy + 2 * n + 1);
      op[n++] = (TessOutPt) { h, k };
    }
  }
  arc[0] = 0;
  for(uint32_t i = 0; i < n; i++) {
    uint32_t j = (i + 1) % n;
    arc[i + 1] = arc[i] + hypotf(xy[2 * j] - xy[2 * i], xy[2 * j + 1] - xy[2 * i + 1]);
  }
  float total = arc[n];
  // For each point: the closest spot on the outline across the inside, if closer than neck
  // and far enough along the outline (not just its own corner)
  uint32_t nc = 0;
  for(uint32_t i = 0; i < n; i++) {
    float bd = neck, bj = -1, bt = 0;
    for(uint32_t j = 0; j < n; j++) {
      uint32_t j1 = (j + 1) % n;
      if(j == i || j1 == i) continue;
      float x0 = xy[2 * j], y0 = xy[2 * j + 1], dx = xy[2 * j1] - x0, dy = xy[2 * j1 + 1] - y0, L2 = dx * dx + dy * dy;
      float t = L2 > 0 ? ((xy[2 * i] - x0) * dx + (xy[2 * i + 1] - y0) * dy) / L2 : 0;
      t = fminf(fmaxf(t, 0), 1);
      float d = hypotf(x0 + dx * t - xy[2 * i], y0 + dy * t - xy[2 * i + 1]);
      if(d >= bd) continue;
      float along = fabsf(arc[j] + t * (arc[j + 1] - arc[j]) - arc[i]);
      if(fminf(along, total - along) < CUTGRAPH_PINCH * neck) continue;
      bd = d; bj = (float)j; bt = t;
    }
    if(bj >= 0) { cand[nc][0] = bd; cand[nc][1] = (float)i; cand[nc][2] = bj; cand[nc][3] = bt; nc++; }
  }
  // Narrowest first
  for(uint32_t a = 1; a < nc; a++) {
    float t[4];
    memcpy(t, cand[a], sizeof t);
    uint32_t b = a;
    for(; b > 0 && cand[b - 1][0] > t[0]; b--) memcpy(cand[b], cand[b - 1], sizeof t);
    memcpy(cand[b], t, sizeof t);
  }
  for(uint32_t c = 0; c < nc && !result; c++) {
    uint32_t i = (uint32_t)cand[c][1], j = (uint32_t)cand[c][2];
    float t = cand[c][3];
    uint32_t j1 = (j + 1) % n;
    float px = xy[2 * i], py = xy[2 * i + 1];
    float qx = xy[2 * j] + (xy[2 * j1] - xy[2 * j]) * t, qy = xy[2 * j + 1] + (xy[2 * j1 + 1] - xy[2 * j + 1]) * t;
    // The cut has to run through the inside
    int inside = 1;
    for(int k = 1; k < 4 && inside; k++) inside = geom_inside(xy, n, px + (qx - px) * k / 4, py + (qy - py) * k / 4);
    if(!inside) continue;
    // Side A: outline from point i to the spot (segments i .. j), side B: the rest
    double areaA = 0, areaB = 0;
    float lx = px, ly = py;
    int borderA = 0, borderB = 0;
    for(uint32_t k = i; ; k = (k + 1) % n) {
      uint32_t k1 = (k + 1) % n;
      const CutEdge * e = g->e + op[k].h / 2;
      int onb = e->left < 0 || e->right < 0;
      float nx = k == j ? qx : xy[2 * k1], ny = k == j ? qy : xy[2 * k1 + 1];
      areaA += lx * ny - nx * ly;
      borderA |= onb;
      lx = nx; ly = ny;
      if(k == j) break;
    }
    areaA += lx * py - px * ly;
    lx = qx; ly = qy;
    for(uint32_t k = j; ; k = (k + 1) % n) {
      uint32_t k1 = (k + 1) % n;
      const CutEdge * e = g->e + op[k].h / 2;
      if(k != j || t < 1) borderB |= e->left < 0 || e->right < 0;
      areaB += lx * xy[2 * k1 + 1] - xy[2 * k1] * ly;
      lx = xy[2 * k1]; ly = xy[2 * k1 + 1];
      if(k1 == i) break;
    }
    areaB += lx * qy - qx * ly;
    areaA = fabs(areaA) / 2;
    areaB = fabs(areaB) / 2;
    int offA = areaA < areaB;   // The smaller side goes
    if(!(offA ? borderA : borderB)) continue;
    // Its neighbour with the most shared edge (not p)
    uint32_t from = offA ? i : j, to = offA ? j : (i + n - 1) % n;
    int32_t best = -1;
    float bl = 0;
    for(uint32_t k = from; ; k = (k + 1) % n) {
      const CutEdge * e = g->e + op[k].h / 2;
      int32_t o = e->left == p ? e->right : e->left;
      if(o >= 0 && o != p) {
        float L = 0;
        for(uint32_t r = from; ; r = (r + 1) % n) {
          const CutEdge * e2 = g->e + op[r].h / 2;
          if((e2->left == p ? e2->right : e2->left) == o) L += arc[r + 1] - arc[r];
          if(r == to) break;
        }
        if(L > bl) { bl = L; best = o; }
      }
      if(k == to) break;
    }
    if(best < 0) continue;
    // Cut: vertices at both ends (splitting edges), the cut-off side to best, then the cut
    if(cutgraph_copy(backup, g)) { result = -1; break; }
    int32_t X = tess_vertexat(g, p, px, py), Y = X < 0 ? -1 : tess_vertexat(g, p, qx, qy);
    if(X < 0 || Y < 0 || X == Y) { result = -1; break; }
    int32_t U = offA ? X : Y, W = offA ? Y : X;
    // Walk p's half-edges from U to W (the cut-off side), handing them to best
    uint32_t cur = (uint32_t)U, guard = 0;
    while(cur != (uint32_t)W && guard++ < g->ne + 1) {
      int found = 0;
      for(uint32_t ei = 0; ei < g->ne && !found; ei++) {
        CutEdge * e = g->e + ei;
        if(e->left == p && e->a == cur) { e->left = best; cur = e->b; found = 1; }
        else if(e->right == p && e->b == cur) { e->right = best; cur = e->a; found = 1; }
      }
      if(!found) break;
    }
    if(cur != (uint32_t)W || tess_grow(g, 0, 1, 0)) { result = -1; break; }
    // Edges now inside best go; then the cut (p walks U -> W along it, best W -> U)
    uint32_t ne = 0;
    for(uint32_t ei = 0; ei < g->ne; ei++) if(g->e[ei].left != g->e[ei].right) g->e[ne++] = g->e[ei];
    g->ne = ne;
    CutEdge * ce = g->e + g->ne++;
    int fwd = U < W;
    *ce = (CutEdge) { .a = (uint32_t)(fwd ? U : W), .b = (uint32_t)(fwd ? W : U), .left = fwd ? p : best, .right = fwd ? best : p };
    result = 1;
  }
done:
  free(xy); free(arc); free(op); free(cand);
  return result;
}

// Neck cuts on every border piece until none are left (or rounds run out); small bits left
// behind join a neighbour as usual
static int tess_necks(CutGraph * g, uint8_t * ori, const TessVary * v, TessStats * stats) {
  for(int round = 0; round < TESS_MAXMERGE; round++) {
    uint32_t cuts = 0;
    for(uint32_t p = 0; p < g->count; p++) {
      if(!tess_isborder(g, p)) continue;
      CutGraph backup = { 0 };
      int r = tess_neckone(g, (int32_t)p, v->neck, &backup);
      if(r == 1 && cutgraph_rewalk(g)) {
        // Left something in two parts or pinched: undo
        cutgraph_free(g);
        *g = backup;
        backup = (CutGraph) { 0 };
        r = cutgraph_rewalk(g) ? -1 : 0;
        stats->neckundo++;
      }
      cutgraph_free(&backup);
      if(r < 0) return 1;
      cuts += r;
    }
    stats->necks += cuts;
    if(!cuts) return 0;
    if(tess_merge(g, ori, v->minarea, v->clear, stats)) return 1;
  }
  return 0;
}

int tesscut_graph(uint16_t cols, uint16_t rows, const TessShape * sh, const TessVary * v, uint32_t seed,
    CutGraph * g, uint8_t ** orient, TessStats * stats) {
  memset(g, 0, sizeof(CutGraph));
  memset(stats, 0, sizeof(TessStats));
  if(orient) *orient = NULL;
  uint32_t target = (uint32_t)cols * rows;
  // Tile radius for one cell of area per tile, then rescaled to the count the border leaves
  int diamond = sh->lattice == TESS_LATTICE_DIAMOND;
  float s = diamond ? sqrtf(0.5f) : sqrtf(2 / (3 * TESS_SQRT3));
  Rng rng;
  rng_init(&rng, seed ^ 0x0FF5E7u);
  float fx = rng_range(&rng, 0, 1), fy = rng_range(&rng, 0, 1);
  CutGraph best = { 0 };
  uint8_t * bestori = NULL;
  TessStats bests = { 0 };
  // A build can fail where an edge crosses the border twice almost at one spot (the crossings'
  // order is then a toss-up): the lattice moves a little and the same size is built again
  int fails = 0;
  for(int fit = 0; fit < TESS_FITS && fails < TESS_MAXFAILS; fit++) {
    TessStats st = { 0 };
    CutGraph full, c;
    uint8_t * ori = NULL;
    // Lattice origin: a random spot within two columns / three rows (diamond: one period), a
    // tile out
    float ox = -s - fx * (diamond ? 2 : 3) * s, oy = -s - fy * (diamond ? 2 * s : 3 * s * TESS_SQRT3);
    if(tess_tiles(cols, rows, s, ox, oy, sh, v, seed, &full, &ori, &st)) break;
    int err = tess_clip(&full, cols, rows, &c);
    cutgraph_free(&full);
    int e2 = err ? 0 : tess_parts(&c, &ori), e3 = err || e2 ? 0 : tess_merge(&c, ori, v->minarea, v->clear, &st);
    if(!err && !e2 && !e3 && v->neck > 0) e3 = tess_necks(&c, ori, v, &st);
    if(err || e2 || e3) {
      if(!err) cutgraph_free(&c);
      free(ori);
      stats->failed++;
      fails++;
      fx = fmodf(fx + 0.37f, 1);
      fy = fmodf(fy + 0.61f, 1);
      fit--;
      continue;
    }
    uint32_t got = c.count;
    if(!best.count || abs((int)c.count - (int)target) < abs((int)best.count - (int)target)) {
      cutgraph_free(&best);
      free(bestori);
      best = c;
      bestori = ori;
      uint32_t failed = stats->failed;
      bests = st;
      bests.failed = failed;
      bests.fits = fit + 1;
    } else {
      cutgraph_free(&c);
      free(ori);
    }
    if(got == target) break;
    s *= sqrtf((float)got / target);
  }
  if(!best.count) return 1;
  *g = best;
  bests.failed = stats->failed;
  *stats = bests;
  if(orient) *orient = bestori;
  else free(bestori);
  return 0;
}

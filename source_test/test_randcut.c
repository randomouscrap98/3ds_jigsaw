#include "test.h"
#include "randcut.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>

static int rc_inside(const float * h, uint32_t n, float x, float y) {
  int in = 0;
  for(uint32_t i = 0, j = n - 1; i < n; j = i++) {
    float xi = h[2 * i], yi = h[2 * i + 1], xj = h[2 * j], yj = h[2 * j + 1];
    if((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
  }
  return in;
}

static void test_randcut_one(uint8_t style, float w, float h, uint32_t count, uint32_t seed) {
  CutGraph c;
  CHECK(randcut_init(&c, w, h, count, seed, randcut_style(style)) == 0);
  if(!c.count) return;
  CHECK(c.count == count);
  // Same seed, same cut
  CutGraph d;
  CHECK(randcut_init(&d, w, h, count, seed, randcut_style(style)) == 0);
  CHECK(d.nv == c.nv && d.ne == c.ne && !memcmp(d.v, c.v, sizeof(CutVert) * c.nv) &&
      !memcmp(d.e, c.e, sizeof(CutEdge) * c.ne));
  cutgraph_free(&d);
  // Every walk entry's edge has this piece on the matching side; inner edges have two sides
  int sides = 1, border = 1, tabless = 0, shapes = 1;
  for(uint32_t p = 0; p < c.count; p++) {
    for(uint32_t k = c.pstart[p]; k < c.pstart[p + 1]; k++) {
      const CutEdge * e = c.e + (c.pedge[k] >> 1);
      sides &= (c.pedge[k] & 1 ? e->right : e->left) == (int32_t)p;
    }
  }
  int tabs[0x1000] = {0};
  for(uint32_t i = 0; i < c.ne; i++) {
    const CutEdge * e = c.e + i;
    if(e->left < 0 || e->right < 0) {
      // Border: on the rectangle, no tabs
      CutVert a = c.v[e->a], b = c.v[e->b];
      border &= e->ntabs == 0 && ((a.x == 0 && b.x == 0) || (a.y == 0 && b.y == 0) ||
          (a.x == w && b.x == w) || (a.y == h && b.y == h));
    } else {
      tabs[e->left] += e->ntabs;
      tabs[e->right] += e->ntabs;
    }
  }
  float * xy = malloc(sizeof(float) * 2 * cutgraph_maxpoints(&c));
  for(uint32_t p = 0; p < c.count; p++) {
    tabless += tabs[p] == 0;
    uint32_t n = cutgraph_outline(&c, p, 1, 0, 0, xy);
    shapes &= edgegen_shape_ok(xy, n, CUTGRAPH_CLEARANCE);
  }
  free(xy);
  CHECK(sides && border && tabless == 0 && shapes);
  // Geometry: hitboxes tile the puzzle (no gaps or overlaps), lookup agrees, neighbours symmetric
  PieceGeo g;
  CHECK(cutgraph_geo(&c, 10, &g) == 0);
  CHECKF(g.width, w * 10); CHECKF(g.height, h * 10);
  Rng r;
  rng_init(&r, seed);
  int gaps = 0, overlaps = 0, wrong = 0;
  for(int i = 0; i < 5000; i++) {
    float x = rng_range(&r, 0, g.width), y = rng_range(&r, 0, g.height);
    int32_t found = -1, hits = 0;
    for(uint32_t q = 0; q < g.count; q++) {
      if(rc_inside(g.hb + 2 * g.hbstart[q], g.hbstart[q + 1] - g.hbstart[q], x, y)) { found = q; hits++; }
    }
    gaps += hits == 0;
    overlaps += hits > 1;
    wrong += piecegeo_at(&g, x, y) != found;
  }
  CHECK(gaps == 0 && overlaps == 0 && wrong == 0);
  int sym = 1;
  for(uint32_t p = 0; p < g.count; p++) {
    for(uint32_t j = g.nbstart[p]; j < g.nbstart[p + 1]; j++) {
      uint16_t q = g.nb[j];
      int back = 0;
      for(uint32_t k = g.nbstart[q]; k < g.nbstart[q + 1]; k++) back |= g.nb[k] == p;
      sym &= back && q != p;
    }
  }
  CHECK(sym);
  piecegeo_free(&g);
  cutgraph_free(&c);
}

void test_randcut(void) {
  for(uint8_t st = 0; st < RANDCUT_STYLES; st++) {
    test_randcut_one(st, 13, 8, 104, 1);
    test_randcut_one(st, 20, 12, 240, 2);
    test_randcut_one(st, 7, 9, 63, 3);
  }
  CutGraph c;
  CHECK(randcut_init(&c, 5, 5, 1, 1, randcut_style(0)) == 1);
}

// A grid graph draws exactly what edgegen draws (whimsy mode keeps grid tabs untouched)
void test_cutgraph_grid(void) {
  for(uint8_t st = 0; st < 3; st++) {
    EdgeGen eg;
    CHECK(edgegen_init(&eg, 9, 7, 55 + st, edgegen_style(st)) == 0);
    CutGraph g;
    CHECK(cutgraph_grid(&g, &eg) == 0);
    CHECK(g.count == 63);
    float a[EDGEGEN_MAXPOINTS * 2], * b = malloc(sizeof(float) * 2 * cutgraph_maxpoints(&g));
    int same = 1;
    for(uint32_t p = 0; p < g.count; p++) {
      uint16_t col = p % 9, row = p / 9;
      int n = edgegen_outline(&eg, col, row, 10, col * 10, row * 10, a);
      uint32_t m = cutgraph_outline(&g, p, 10, 0, 0, b);
      if((uint32_t)n != m) { same = 0; continue; }
      for(int i = 0; i < 2 * n; i++) same &= fabsf(a[i] - b[i]) < 1e-3f;
    }
    CHECK(same);
    free(b);
    cutgraph_free(&g);
    edgegen_free(&eg);
  }
}

#include "cutter.h"

// Whimsy cuts: same checks as random cuts, for every style and level
void test_whimsy(void) {
  for(uint8_t st = 0; st < CUT_STYLES; st++) {
    for(uint8_t wh = WHIMSY_FEW; wh <= WHIMSY_MANY; wh++) {
      Cutter c, d;
      CHECK(cutter_init(&c, st, wh, WHIMSY_SET_CUTE, 16, 11, 300 + st) == 0);
      CHECK(cutter_init(&d, st, wh, WHIMSY_SET_CUTE, 16, 11, 300 + st) == 0);
      CHECK(c.whimsies > 0 && c.count == 16 * 11 + c.whimsies && c.usegraph);
      const CutGraph * g = &c.g;
      CHECK(d.count == c.count && d.g.ne == g->ne && !memcmp(d.g.e, g->e, sizeof(CutEdge) * g->ne) &&
          d.g.npts == g->npts && !memcmp(d.g.pts, g->pts, sizeof(float) * 2 * g->npts));
      cutter_free(&d);
      int sides = 1, tabless = 0, shapes = 1, whim = 0;
      for(uint32_t p = 0; p < g->count; p++) {
        int tabs = 0;
        for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
          const CutEdge * e = g->e + (g->pedge[k] >> 1);
          sides &= (g->pedge[k] & 1 ? e->right : e->left) == (int32_t)p;
          tabs += e->ntabs;
        }
        int isw = cutgraph_iswhimsy(g, p);
        whim += isw;
        // Whimsies come after the grid/random pieces
        sides &= isw == (p >= 16 * 11);
        tabless += !isw && !tabs;
      }
      float * xy = malloc(sizeof(float) * 2 * c.maxpoints);
      for(uint32_t p = 0; p < g->count; p++) {
        uint32_t n = cutter_outline(&c, p, 1, 0, 0, xy);
        shapes &= edgegen_shape_ok(xy, n, CUTGRAPH_CLEARANCE);
      }
      free(xy);
      CHECK(sides && tabless == 0 && shapes && whim == (int)c.whimsies);
      PieceGeo geo;
      CHECK(cutter_geo(&c, 10, &geo) == 0);
      Rng r;
      rng_init(&r, st);
      int gaps = 0, overlaps = 0, wrong = 0;
      for(int i = 0; i < 4000; i++) {
        float x = rng_range(&r, 0, geo.width), y = rng_range(&r, 0, geo.height);
        int32_t found = -1, hits = 0;
        for(uint32_t q = 0; q < geo.count; q++) {
          if(rc_inside(geo.hb + 2 * geo.hbstart[q], geo.hbstart[q + 1] - geo.hbstart[q], x, y)) { found = q; hits++; }
        }
        gaps += hits == 0;
        overlaps += hits > 1;
        wrong += piecegeo_at(&geo, x, y) != found;
      }
      CHECK(gaps == 0 && overlaps == 0 && wrong == 0);
      piecegeo_free(&geo);
      cutter_free(&c);
    }
  }
}

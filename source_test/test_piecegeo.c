#include "test.h"
#include "geom.h"
#include "piecegeo.h"
#include "edgegen.h"
#include "rng.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// Jittered corners (wild cut): hitboxes are the corner quads, cover the puzzle with no
// gaps or overlaps, and the lookup grid agrees with a brute force search
static void test_piecegeo_corners(void) {
  const uint16_t cols = 26, rows = 19;
  const float cell = 37;
  EdgeGen eg;
  CHECK(edgegen_init(&eg, cols, rows, 777, edgegen_style(EDGEGEN_STYLE_WILD)) == 0);
  PieceGeo g;
  CHECK(piecegeo_grid(&g, cols, rows, cell, eg.corners) == 0);
  // Hitbox corner = the jittered grid corner
  const float * c = eg.corners + 2 * (3 * (cols + 1) + 5);   // Corner (5, 3)
  uint32_t p = 3 * cols + 5;                                  // Its top-left corner
  CHECKF(g.hb[2 * g.hbstart[p]], (5 + c[0]) * cell);
  CHECKF(g.hb[2 * g.hbstart[p] + 1], (3 + c[1]) * cell);
  Rng r;
  rng_init(&r, 1);
  int gaps = 0, overlaps = 0, wrong = 0;
  for(int i = 0; i < 20000; i++) {
    float x = rng_range(&r, 0, g.width), y = rng_range(&r, 0, g.height);
    int32_t found = -1, hits = 0;
    for(uint32_t q = 0; q < g.count; q++) {
      if(geom_inside(g.hb + 2 * g.hbstart[q], g.hbstart[q + 1] - g.hbstart[q], x, y)) {
        found = q;
        hits++;
      }
    }
    gaps += hits == 0;
    overlaps += hits > 1;
    wrong += piecegeo_at(&g, x, y) != found;
  }
  CHECK(gaps == 0 && overlaps == 0 && wrong == 0);
  piecegeo_free(&g);
  edgegen_free(&eg);
}

// Write/read round trip, plus truncated and corrupted buffers (ASan catches bad reads)
static void test_piecegeo_save(void) {
  EdgeGen eg;
  CHECK(edgegen_init(&eg, 9, 7, 31, edgegen_style(EDGEGEN_STYLE_IRREGULAR)) == 0);
  PieceGeo g, h;
  CHECK(piecegeo_grid(&g, 9, 7, 21, eg.corners) == 0);
  size_t len = piecegeo_size(&g);
  uint8_t * buf = malloc(len);
  CHECK(piecegeo_write(&g, buf) == len);
  CHECK(piecegeo_read(&h, buf, len) == 0);
  CHECK(h.count == g.count && h.cols == 9 && h.rows == 7);
  CHECKF(h.cell, 21); CHECKF(h.width, g.width); CHECKF(h.height, g.height);
  CHECK(memcmp(h.center, g.center, sizeof(float) * 2 * g.count) == 0);
  CHECK(memcmp(h.nbstart, g.nbstart, sizeof(uint32_t) * (g.count + 1)) == 0);
  CHECK(memcmp(h.nb, g.nb, sizeof(uint16_t) * g.nbstart[g.count]) == 0);
  CHECK(memcmp(h.hbstart, g.hbstart, sizeof(uint32_t) * (g.count + 1)) == 0);
  CHECK(memcmp(h.hb, g.hb, sizeof(float) * 2 * g.hbstart[g.count]) == 0);
  int same = 1;
  for(float y = 0; y < g.height; y += 2.5f) for(float x = 0; x < g.width; x += 2.5f) {
    same &= piecegeo_at(&g, x, y) == piecegeo_at(&h, x, y);
  }
  CHECK(same);
  piecegeo_free(&h);
  // Every truncation fails cleanly
  int ok = 0;
  for(size_t n = 0; n < len; n++) {
    uint8_t * cut = malloc(n ? n : 1);
    memcpy(cut, buf, n);
    ok += piecegeo_read(&h, cut, n) == 0;
    free(cut);
  }
  CHECK(ok == 0);
  // Corrupted bytes: may or may not be rejected, but must never read out of range
  Rng r;
  rng_init(&r, 3);
  for(int i = 0; i < 3000; i++) {
    uint8_t * bad = malloc(len);
    memcpy(bad, buf, len);
    for(int k = 0; k < 3; k++) bad[rng_below(&r, len)] = rng_next(&r);
    if(piecegeo_read(&h, bad, len) == 0) {
      for(int k = 0; k < 20; k++) piecegeo_at(&h, rng_range(&r, -10, h.width + 10), rng_range(&r, -10, h.height + 10));
      piecegeo_free(&h);
    }
    free(bad);
  }
  buf[4] = PIECEGEO_VERSION + 1;
  CHECK(piecegeo_read(&h, buf, len) == 2);
  free(buf);
  piecegeo_free(&g);
  edgegen_free(&eg);
}

void test_piecegeo(void) {
  test_piecegeo_corners();
  test_piecegeo_save();
  const uint16_t cols = 7, rows = 5;
  const float cell = 13;
  PieceGeo g;
  CHECK(piecegeo_grid(&g, 0, 5, cell, NULL) == 1);
  CHECK(piecegeo_grid(&g, cols, rows, cell, NULL) == 0);
  CHECK(g.count == 35);
  CHECKF(g.width, 91); CHECKF(g.height, 65);
  CHECKF(g.center[2 * 9], 2.5f * cell); CHECKF(g.center[2 * 9 + 1], 1.5f * cell);

  // Neighbours: grid order (up, down, left, right), symmetric
  for(uint32_t p = 0; p < g.count; p++) {
    uint32_t col = p % cols, row = p / cols;
    int32_t want[4] = {
      row > 0 ? (int32_t)(p - cols) : -1, row + 1 < rows ? (int32_t)(p + cols) : -1,
      col > 0 ? (int32_t)(p - 1) : -1, col + 1 < cols ? (int32_t)(p + 1) : -1,
    };
    uint32_t i = g.nbstart[p];
    for(int k = 0; k < 4; k++) {
      if(want[k] < 0) continue;
      CHECK(i < g.nbstart[p + 1] && g.nb[i] == want[k]);
      i++;
    }
    CHECK(i == g.nbstart[p + 1]);
    for(uint32_t j = g.nbstart[p]; j < g.nbstart[p + 1]; j++) {
      uint16_t q = g.nb[j];
      int back = 0;
      for(uint32_t k = g.nbstart[q]; k < g.nbstart[q + 1]; k++) back |= g.nb[k] == p;
      CHECK(back);
    }
  }

  // Lookup matches the old cell arithmetic everywhere, edges included (half-open cells)
  int bad = 0;
  for(float y = -cell; y <= g.height + cell; y += cell / 4) {
    for(float x = -cell; x <= g.width + cell; x += cell / 4) {
      int32_t want = -1;
      if(x >= 0 && y >= 0 && x < g.width && y < g.height) want = (int32_t)(y / cell) * cols + (int32_t)(x / cell);
      if(piecegeo_at(&g, x, y) != want) bad++;
    }
  }
  CHECK(bad == 0);
  CHECK(piecegeo_at(&g, NAN, 5) == -1);
  piecegeo_free(&g);
  CHECK(g.center == NULL && g.count == 0);
}

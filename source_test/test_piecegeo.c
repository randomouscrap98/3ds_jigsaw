#include "test.h"
#include "piecegeo.h"

#include <math.h>

void test_piecegeo(void) {
  const uint16_t cols = 7, rows = 5;
  const float cell = 13;
  PieceGeo g;
  CHECK(piecegeo_grid(&g, 0, 5, cell) == 1);
  CHECK(piecegeo_grid(&g, cols, rows, cell) == 0);
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

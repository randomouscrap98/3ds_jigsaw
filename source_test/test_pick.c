// Picking with loose grab shapes (maze pieces) must leave every other style as it was,
// and the packed scatter has to fit maze pieces on the table
#include "test.h"
#include "board.h"
#include "cutter.h"
#include "geom.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>

// board_pick before loose grab shapes existed
static int32_t pick_old(Board * b, float x, float y) {
  for(int32_t i = (int32_t)b->ngroups - 1; i >= 0; i--) {
    uint16_t gid = b->order[i];
    BoardGroup * g = b->groups + gid;
    float lx, ly;
    board_rotvec((4 - g->rot) & 3, x - g->x, y - g->y, &lx, &ly);
    int32_t p = piecegeo_at(b->geo, lx, ly);
    if(p >= 0 && b->pieces[p].group == gid) return gid;
  }
  return -1;
}

void test_pick_same(void) {
  for(uint8_t st = 0; st < CUT_STYLES; st++) {
    if(st == CUT_STYLE_MAZE) continue;
    for(uint8_t wh = 0; wh < 2; wh++) {
      Cutter c;
      CHECK(cutter_init(&c, st, wh ? WHIMSY_MANY : WHIMSY_NONE, WHIMSY_SET_CUTE, 12, 9, 50 + st) == 0);
      PieceGeo geo;
      CHECK(cutter_geo(&c, 10, &geo) == 0);
      CHECK(geo.hull == NULL);
      Board b;
      float tw = geo.width * 3, th = geo.height * 3;
      CHECK(board_init(&b, &geo, tw, th) == 0);
      Rng r;
      rng_init(&r, st * 7 + wh);
      int same = 1;
      for(int layout = 0; layout < 2; layout++) {
        // Solved (every piece at home) and scattered (rotated, overlapping)
        if(layout) board_scatter(&b, 99 + st, tw / 6, th / 6, tw * 0.67f, th * 0.67f);
        for(int i = 0; i < 20000; i++) {
          float x = rng_range(&r, 0, layout ? tw : geo.width), y = rng_range(&r, 0, layout ? th : geo.height);
          same &= board_pick(&b, x, y) == pick_old(&b, x, y);
        }
      }
      CHECK(same);
      board_free(&b);
      piecegeo_free(&geo);
      cutter_free(&c);
    }
  }
}

// Maze: hulls hold their pieces, every piece is picked at its own cells after the packed
// scatter, and the pack fits the game's scatter region (GAME_TABLESCALE_MAZE 3.5, 0.67 of it)
void test_pick_maze(void) {
  const uint16_t sizes[3][2] = { { 8, 6 }, { 12, 9 }, { 26, 19 } };
  for(int si = 0; si < 3; si++) {
    uint16_t cols = sizes[si][0], rows = sizes[si][1];
    Cutter c;
    CHECK(cutter_init(&c, CUT_STYLE_MAZE, WHIMSY_NONE, 0, cols, rows, 7 + si) == 0);
    CHECK(c.count == (uint32_t)cols * rows);
    PieceGeo geo;
    CHECK(cutter_geo(&c, 10, &geo) == 0);
    CHECK(piecegeo_hulls(&geo) == 0);
    int inhull = 1;
    for(uint32_t p = 0; p < geo.count; p++) {
      const float * h = geo.hb + 2 * geo.hbstart[p];
      for(uint32_t k = geo.hbstart[p]; k < geo.hbstart[p + 1]; k++, h += 2) {
        // Corners nudged toward the home center (a corner itself may sit on the hull's edge)
        float x = h[0] + (geo.center[2 * p] - h[0]) * 0.01f, y = h[1] + (geo.center[2 * p + 1] - h[1]) * 0.01f;
        inhull &= piecegeo_inhull(&geo, p, x, y) || !geom_inside(geo.hb + 2 * geo.hbstart[p], geo.hbstart[p + 1] - geo.hbstart[p], x, y);
      }
    }
    CHECK(inhull);
    Board b;
    float tw = geo.width * 3.5f, th = geo.height * 3.5f, sw = tw * 0.67f, sh = th * 0.67f;
    CHECK(board_init(&b, &geo, tw, th) == 0);
    int fit = board_scatterpack(&b, 3 + si, (tw - sw) / 2, (th - sh) / 2, sw, sh) == 0;
    // Every piece's own body points (just inside) pick its group: nothing covers another
    int picked = 1;
    for(uint32_t p = 0; p < geo.count; p++) {
      const BoardGroup * g = b.groups + b.pieces[p].group;
      // A body point: midpoint of the first hitbox edge, nudged inside by testing both sides
      const float * h = geo.hb + 2 * geo.hbstart[p];
      float mx = (h[0] + h[2]) / 2, my = (h[1] + h[3]) / 2, dx = h[3] - h[1], dy = h[0] - h[2];
      float len = sqrtf(dx * dx + dy * dy), e = 0.2f;
      float ax = mx + dx / len * e, ay = my + dy / len * e;
      if(!geom_inside(h, geo.hbstart[p + 1] - geo.hbstart[p], ax, ay)) { ax = mx - dx / len * e; ay = my - dy / len * e; }
      float wx, wy;
      board_rotvec(g->rot, ax, ay, &wx, &wy);
      picked &= board_pick(&b, g->x + wx, g->y + wy) == b.pieces[p].group;
    }
    printf("  maze %ux%u: pack %s\n", cols, rows, fit ? "fits" : "DOESN'T FIT");
    CHECK(fit && picked);
    board_free(&b);
    piecegeo_free(&geo);
    cutter_free(&c);
  }
}

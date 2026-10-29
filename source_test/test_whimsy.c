#include "test.h"
#include "geom.h"
#include "cutter.h"
#include "boardsave.h"
#include "savefile.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>

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
          tabs += e->ntabs + !!(e->flags & CUTEDGE_SNAKE);   // A snake holds its pieces
        }
        int isw = cutgraph_iswhimsy(g, p);
        whim += isw;
        // Whimsies come after the grid/random pieces
        sides &= isw == (p >= 16 * 11);
        tabless += !isw && !tabs && st != CUT_STYLE_WOODEN3;   // Wooden3 pieces hold by shape
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
          if(geom_inside(geo.hb + 2 * geo.hbstart[q], geo.hbstart[q + 1] - geo.hbstart[q], x, y)) { found = q; hits++; }
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


// Saves with whimsies: boardsave v2 carries the piece count; boards without stay v1
void test_boardsave_whimsy(void) {
  Cutter c;
  CHECK(cutter_init(&c, CUT_STYLE_CLASSIC, WHIMSY_MANY, WHIMSY_SET_CUTE, 12, 9, 77) == 0);
  CHECK(c.count > 12 * 9);
  PieceGeo g;
  CHECK(cutter_geo(&c, 20, &g) == 0);
  g.cols = 12;
  g.rows = 9;
  Board b, d;
  CHECK(board_init(&b, &g, 800, 600) == 0);
  board_scatter(&b, 5, 0, 0, 800, 600);
  SaveHeader h;
  savefile_init(&h);
  h.style = CUT_STYLE_CLASSIC;
  h.target = 108;
  h.whimsy = WHIMSY_MANY;
  h.whimsyset = WHIMSY_SET_CUTE;
  h.cuthash = cutter_hash(&c);
  CHECK(h.cuthash != 0);
  size_t len = savefile_size(&b);
  uint8_t * buf = malloc(len);
  savefile_write(&h, &b, buf);
  SaveInfo info;
  CHECK(savefile_peek(buf, len, &info) == SAVEFILE_OK && info.pieces == c.count);
  CHECK(info.header.whimsy == WHIMSY_MANY && info.header.whimsyset == WHIMSY_SET_CUTE);
  CHECK(info.header.cuthash == cutter_hash(&c));
  BoardSaveInfo bi;
  CHECK(boardsave_peek(buf + SAVEFILE_HEADER, len - SAVEFILE_HEADER, &bi) == 0 && bi.version == 2 && bi.count == c.count);
  CHECK(board_init(&d, &g, 800, 600) == 0);
  SaveHeader rh;
  CHECK(savefile_readheader(buf, len, &rh) == SAVEFILE_OK);
  CHECK(savefile_readboard(buf, len, &rh, &d) == SAVEFILE_OK);
  int same = d.ngroups == b.ngroups;
  for(uint32_t p = 0; p < b.count && same; p++) {
    float x0, y0, x1, y1;
    board_piece_center(&b, p, &x0, &y0);
    board_piece_center(&d, p, &x1, &y1);
    same = fabsf(x0 - x1) < 1e-3f && fabsf(y0 - y1) < 1e-3f && board_piece_rot(&b, p) == board_piece_rot(&d, p);
  }
  CHECK(same);
  board_free(&d);
  // A board with a different piece count (e.g. the same picture without whimsies) is refused
  PieceGeo plain;
  CHECK(piecegeo_grid(&plain, 12, 9, 20, NULL) == 0);
  CHECK(board_init(&d, &plain, 800, 600) == 0);
  CHECK(savefile_readboard(buf, len, &rh, &d) == SAVEFILE_BAD);
  // ...and a plain board writes version 1 (readable by 1.0-1.1)
  size_t plen = boardsave_size(&d);
  uint8_t * pbuf = malloc(plen);
  CHECK(boardsave_write(&d, pbuf) == plen);
  CHECK(boardsave_peek(pbuf, plen, &bi) == 0 && bi.version == 1 && bi.count == 12 * 9 && plen == 18 + 9 * d.ngroups + 2 * 108);
  // A 1.1 header (24 bytes) reads as no whimsies
  buf[10] = 24;
  CHECK(savefile_readheader(buf, len, &rh) == SAVEFILE_OK && rh.whimsy == 0 && rh.whimsyset == 0 && rh.cuthash == 0);
  free(pbuf);
  free(buf);
  board_free(&d);
  board_free(&b);
  piecegeo_free(&plain);
  piecegeo_free(&g);
  cutter_free(&c);
}

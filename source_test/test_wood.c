// Shelved wooden cuts (woodcut): not in the app, so not in the default run (tests wood)
#include "test.h"
#include "geom.h"
#include "edgegen.h"
#include "strokecut.h"
#include "woodcut.h"

#include <stdlib.h>
#include <string.h>

// All provisional (previews). Style order: Wooden .. Wooden5
static const uint32_t wood_pinned[5] = { 0x72D29B9Fu, 0xBB1D73F9u, 0xE5BAC95Bu, 0x953877A3u, 0u };
static const uint32_t wood_pinnedw[5][2] = { { 0x025506C2u, 0xF89FC3D4u }, { 0xE1015134u, 0x90AEF2CDu },
    { 0xC2BF74A1u, 0x350E7B3Fu }, { 0x953877A3u, 0x953877A3u }, { 0u, 0u } };   // Few/Many; Wooden4 has no whimsies

static uint32_t wood_hash(uint8_t style, uint8_t whimsy) {
  Cutter cut;
  if(woodcut_init(&cut, style, whimsy, WHIMSY_SET_CUTE, 13, 9, 4242)) return 0;
  uint32_t h = cutter_hash(&cut);
  cutter_free(&cut);
  return h;
}

// Stroke cut (lines only so far): exact piece count (+ whimsies), outlines tile the puzzle,
// clean shapes, same seed = same cut
static void test_strokecut(void) {
  const uint16_t sizes[3][2] = { { 7, 5 }, { 15, 10 }, { 28, 17 } };
  for(int si = 0; si < 3; si++) {
    for(uint8_t wh = 0; wh < WHIMSY_LEVELS; wh++) {
      uint16_t w = sizes[si][0], h = sizes[si][1];
      CutGraph c, d;
      CHECK(strokecut_graph(&c, w, h, w * h, wh, WHIMSY_SET_CUTE, 99 + si) == 0);
      CHECK(strokecut_graph(&d, w, h, w * h, wh, WHIMSY_SET_CUTE, 99 + si) == 0);
      CHECK(c.count >= (uint32_t)w * h && c.count <= (uint32_t)w * h + whimsy_count(wh, w * h));   // Whimsies can be dropped
      CHECK(d.count == c.count && d.ne == c.ne && d.npts == c.npts &&
          !memcmp(d.pts, c.pts, sizeof(float) * 2 * c.npts));
      float * xy = malloc(sizeof(float) * 2 * cutgraph_maxpoints(&c));
      double area = 0;
      int shapes = 1;
      for(uint32_t p = 0; p < c.count; p++) {
        uint32_t n = cutgraph_outline(&c, p, 1, 0, 0, xy);
        area += geom_area(xy, n, NULL, NULL);
        shapes &= edgegen_shape_ok(xy, n, 0.01f);
      }
      CHECK(fabs(area - (double)w * h) < 1e-3 * w * h && shapes);
      free(xy);
      cutgraph_free(&c);
      cutgraph_free(&d);
    }
  }
}

void test_wood(void) {
  for(uint8_t st = CUT_STYLE_WOODEN; st < WOODCUT_STYLES_END; st++) {
    for(uint8_t wh = 0; wh < 3; wh++) {
      uint32_t h = wood_hash(st, wh), want = wh ? wood_pinnedw[st - CUT_STYLE_WOODEN][wh - 1] : wood_pinned[st - CUT_STYLE_WOODEN];
      if(h != want) printf("  style %u whimsy %u hash %08lX\n", st, wh, (unsigned long)h);
      CHECK(h == want);
    }
    test_whimsy_style(st);
  }
  test_randcut_style(RANDCUT_STYLE_WOOD);
  test_randcut_style(RANDCUT_STYLE_WOOD2);
  test_strokecut();
}

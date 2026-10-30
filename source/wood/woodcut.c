#include "woodcut.h"
#include "strokecut.h"
#include "pottscut.h"
#include "snakecut.h"

#include <string.h>

int woodcut_init(Cutter * c, uint8_t style, uint8_t whimsy, uint8_t set, uint16_t cols, uint16_t rows, uint32_t seed) {
  if(style < CUT_STYLES || style >= WOODCUT_STYLES_END) return cutter_init(c, style, whimsy, set, cols, rows, seed);
  memset(c, 0, sizeof(Cutter));
  c->style = style;
  c->whimsy = whimsy < WHIMSY_LEVELS ? whimsy : WHIMSY_NONE;
  c->set = set < WHIMSY_SETS ? set : WHIMSY_SET_CUTE;
  c->cols = cols;
  c->rows = rows;
  c->count = (uint32_t)cols * rows;
  if(style == CUT_STYLE_WOODEN) return cutter_graph(c, randcut_style(RANDCUT_STYLE_WOOD), seed);
  c->usegraph = 1;
  // Stroke / Potts cut: whimsies are part of them (placed first). Wooden2's tab rules for all.
  CutTabStyle ts;
  randcut_tabstyle(randcut_style(RANDCUT_STYLE_WOOD2), &ts);
  if(c->whimsy != WHIMSY_NONE) ts.whimsytab = WHIMSY_TABEDGE;
  int ok = 0;
  for(uint32_t t = 0; t < RANDCUT_MAXTRIES && !ok; t++) {
    uint32_t s = randcut_tryseed(seed, t);
    int gerr = style == CUT_STYLE_WOODEN2 ? strokecut_graph(&c->g, cols, rows, c->count, c->whimsy, c->set, s)
        : style == CUT_STYLE_WOODEN4 ? snakecut_graph(&c->g, cols, rows, c->count, c->whimsy, c->set, s, snakecut_style(SNAKECUT_STYLE_DEFAULT))
        : style == CUT_STYLE_WOODEN5 ? snakecut_graph(&c->g, cols, rows, c->count, c->whimsy, c->set, s, snakecut_style(SNAKECUT_STYLE_TJUNCTION))
        : pottscut_graph(&c->g, cols, rows, c->count, c->whimsy, c->set, s, pottscut_style(POTTSCUT_STYLE_DEFAULT));
    if(gerr) continue;
    if(style == CUT_STYLE_WOODEN3 || style == CUT_STYLE_WOODEN4) {
      // Potts / snake pieces interlock by their shapes: no knobs
      c->g.tries = t + 1;
      ok = 1;
      break;
    }
    if(!cutgraph_tabs(&c->g, &ts, s)) {
      c->g.tries = t + 1;
      ok = 1;
    } else {
      cutgraph_free(&c->g);
    }
  }
  if(!ok) return 1;
  c->whimsies = c->g.count - c->count;
  c->count = c->g.count;
  c->maxpoints = cutgraph_maxpoints(&c->g);
  return 0;
}

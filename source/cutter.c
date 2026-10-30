#include "cutter.h"
#include "strokecut.h"
#include "pottscut.h"
#include "snakecut.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

int cutter_init(Cutter * c, uint8_t style, uint8_t whimsy, uint8_t set, uint16_t cols, uint16_t rows, uint32_t seed) {
  memset(c, 0, sizeof(Cutter));
  c->style = style < CUT_STYLES ? style : CUT_STYLE_CLASSIC;
  c->whimsy = whimsy < WHIMSY_LEVELS ? whimsy : WHIMSY_NONE;
  c->set = set < WHIMSY_SETS ? set : WHIMSY_SET_CUTE;
  c->cols = cols;
  c->rows = rows;
  c->count = (uint32_t)cols * rows;
  int rnd = c->style >= CUT_STYLE_RANDOM;
  const RandCutStyle * rst = randcut_style(c->style == CUT_STYLE_RANDRECT ? RANDCUT_STYLE_RECT :
      c->style == CUT_STYLE_WOODEN ? RANDCUT_STYLE_WOOD : c->style == CUT_STYLE_WOODEN2 ? RANDCUT_STYLE_WOOD2 :
      RANDCUT_STYLE_EVEN);
  if(!rnd && c->whimsy == WHIMSY_NONE) {
    if(edgegen_init(&c->eg, cols, rows, seed, edgegen_style(c->style))) return 1;
    c->maxpoints = EDGEGEN_MAXPOINTS;
    return 0;
  }
  c->usegraph = 1;
  if(c->style == CUT_STYLE_WOODEN2 || c->style == CUT_STYLE_WOODEN3 || c->style == CUT_STYLE_WOODEN4 ||
      c->style == CUT_STYLE_WOODEN5) {
    // Stroke / Potts cut: whimsies are part of them (placed first). Wooden2's tab rules for both.
    CutTabStyle ts;
    randcut_tabstyle(randcut_style(RANDCUT_STYLE_WOOD2), &ts);
    if(c->whimsy != WHIMSY_NONE) ts.whimsytab = WHIMSY_TABEDGE;
    int ok = 0;
    for(uint32_t t = 0; t < RANDCUT_MAXTRIES && !ok; t++) {
      uint32_t s = randcut_tryseed(seed, t);
      int gerr = c->style == CUT_STYLE_WOODEN2 ? strokecut_graph(&c->g, cols, rows, c->count, c->whimsy, c->set, s)
          : c->style == CUT_STYLE_WOODEN4 ? snakecut_graph(&c->g, cols, rows, c->count, c->whimsy, c->set, s, snakecut_style(SNAKECUT_STYLE_DEFAULT))
          : c->style == CUT_STYLE_WOODEN5 ? snakecut_graph(&c->g, cols, rows, c->count, c->whimsy, c->set, s, snakecut_style(SNAKECUT_STYLE_TJUNCTION))
          : pottscut_graph(&c->g, cols, rows, c->count, c->whimsy, c->set, s, pottscut_style(POTTSCUT_STYLE_DEFAULT));
      if(gerr) continue;
      if(c->style == CUT_STYLE_WOODEN3 || c->style == CUT_STYLE_WOODEN4) {
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
  } else if(rnd && c->whimsy == WHIMSY_NONE) {
    if(randcut_init(&c->g, cols, rows, c->count, seed, rst)) return 1;
  } else {
    // Whimsies go in before tabs; a cut that can't be tabbed cleanly is retried with new
    // whimsy spots (and for random, new points)
    CutTabStyle ts;
    randcut_tabstyle(rst, &ts);
    ts.whimsytab = WHIMSY_TABEDGE;
    if(!rnd) {
      ts.jitter = edgegen_style(c->style);
      if(edgegen_init(&c->eg, cols, rows, seed, edgegen_style(c->style))) return 1;
    }
    int ok = 0;
    for(uint32_t t = 0; t < CUTTER_MAXTRIES && !ok; t++) {
      uint32_t s = randcut_tryseed(seed, t);
      int err = rnd ? randcut_graph(&c->g, cols, rows, c->count, s, rst)
          : cutgraph_grid(&c->g, &c->eg);
      if(err) continue;
      c->g.tries = t + 1;
      // A cut with whimsies asked for gets at least one (or tries again)
      int placed = whimsy_add(&c->g, c->whimsy, c->set, s);
      if(placed > 0 && !(rnd ? randcut_tabs(&c->g, rst, &ts, s) : cutgraph_tabs(&c->g, &ts, s))) {
        c->whimsies = placed;
        ok = 1;
      } else {
        cutgraph_free(&c->g);
      }
    }
    // Last resort, so a puzzle always gets cut: the same cut without whimsies (still the
    // same every time for this seed)
    if(!ok) {
      c->whimsies = 0;
      if(rnd) ok = !randcut_init(&c->g, cols, rows, c->count, seed, rst);
      else ok = !cutgraph_grid(&c->g, &c->eg) && !cutgraph_tabs(&c->g, &ts, seed);
      c->g.tries = CUTTER_MAXTRIES + 1;
    }
    if(!rnd) edgegen_free(&c->eg);
    if(!ok) {
      cutgraph_free(&c->g);
      return 1;
    }
  }
  c->count = c->g.count;
  c->maxpoints = cutgraph_maxpoints(&c->g);
  return 0;
}

void cutter_free(Cutter * c) {
  if(c->usegraph) cutgraph_free(&c->g);
  else edgegen_free(&c->eg);
  memset(c, 0, sizeof(Cutter));
}

uint32_t cutter_outline(const Cutter * c, uint32_t p, float cell, float ox, float oy, float * xy) {
  if(c->usegraph) return cutgraph_outline(&c->g, p, cell, ox, oy, xy);
  uint16_t col = p % c->cols, row = p / c->cols;
  return edgegen_outline(&c->eg, col, row, cell, ox + col * cell, oy + row * cell, xy);
}

int cutter_bounds(const Cutter * c, float * boxes) {
  float * xy = malloc(sizeof(float) * 2 * c->maxpoints);
  if(!xy) return 1;
  for(uint32_t p = 0; p < c->count; p++) {
    uint32_t n = cutter_outline(c, p, 1, 0, 0, xy);
    float * b = boxes + 4 * p;
    b[0] = b[2] = xy[0];
    b[1] = b[3] = xy[1];
    for(uint32_t i = 1; i < n; i++) {
      if(xy[i * 2] < b[0]) b[0] = xy[i * 2];
      if(xy[i * 2] > b[2]) b[2] = xy[i * 2];
      if(xy[i * 2 + 1] < b[1]) b[1] = xy[i * 2 + 1];
      if(xy[i * 2 + 1] > b[3]) b[3] = xy[i * 2 + 1];
    }
  }
  free(xy);
  return 0;
}

int cutter_geo(const Cutter * c, float cell, PieceGeo * g) {
  if(!c->usegraph) return piecegeo_grid(g, c->cols, c->rows, cell, c->eg.corners);
  if(cutgraph_geo(&c->g, cell, g)) return 1;
  g->cols = c->cols;
  g->rows = c->rows;
  return 0;
}

uint32_t cutter_hash(const Cutter * c) {
  float * xy = malloc(sizeof(float) * 2 * c->maxpoints);
  if(!xy) return 1;
  uint32_t h = 2166136261u;   // FNV-1a over point counts and rounded coordinates
  for(uint32_t p = 0; p < c->count; p++) {
    uint32_t n = cutter_outline(c, p, 1, 0, 0, xy);
    h = (h ^ n) * 16777619u;
    for(uint32_t i = 0; i < 2 * n; i++) h = (h ^ (uint32_t)(int32_t)lroundf(xy[i] * 1000)) * 16777619u;
  }
  free(xy);
  return h ? h : 1;
}

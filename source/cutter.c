#include "cutter.h"
#include "mazepiece.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

// Maze pieces: no whimsies; a cut with a repeated piece shape is retried with a new seed
static int cutter_maze(Cutter * c, uint32_t seed) {
  c->whimsy = WHIMSY_NONE;
  c->usegraph = 1;
  for(uint32_t t = 0; t < CUTTER_MAZETRIES; t++) {
    MazePieces m;
    if(mazepiece_init(&m, c->cols, c->rows, c->count, CUTTER_MAZEAVG, CUTTER_MAZENEWEST, 0, randcut_tryseed(seed, t))) continue;
    int err = m.dups || mazepiece_graph(&m, c->cols, c->rows, &c->g);
    mazepiece_free(&m);
    if(err) continue;
    c->g.tries = t + 1;
    c->maxpoints = cutgraph_maxpoints(&c->g);
    return 0;
  }
  return 1;
}

int cutter_init(Cutter * c, uint8_t style, uint8_t whimsy, uint8_t set, uint16_t cols, uint16_t rows, uint32_t seed) {
  memset(c, 0, sizeof(Cutter));
  c->style = style < CUT_STYLES ? style : CUT_STYLE_CLASSIC;
  c->whimsy = whimsy < WHIMSY_LEVELS ? whimsy : WHIMSY_NONE;
  c->set = set < WHIMSY_SETS ? set : WHIMSY_SET_CUTE;
  c->cols = cols;
  c->rows = rows;
  c->count = (uint32_t)cols * rows;
  if(c->style == CUT_STYLE_MAZE) return cutter_maze(c, seed);
  if(c->style < CUT_STYLE_RANDOM && c->whimsy == WHIMSY_NONE) {
    if(edgegen_init(&c->eg, cols, rows, seed, edgegen_style(c->style))) return 1;
    c->maxpoints = EDGEGEN_MAXPOINTS;
    return 0;
  }
  return cutter_graph(c, c->style < CUT_STYLE_RANDOM ? NULL :
      randcut_style(c->style == CUT_STYLE_RANDRECT ? RANDCUT_STYLE_RECT : RANDCUT_STYLE_EVEN), seed);
}

int cutter_graph(Cutter * c, const RandCutStyle * rst, uint32_t seed) {
  int rnd = rst != NULL;
  uint16_t cols = c->cols, rows = c->rows;
  c->usegraph = 1;
  if(rnd && c->whimsy == WHIMSY_NONE) {
    if(randcut_init(&c->g, cols, rows, c->count, seed, rst)) return 1;
  } else {
    // Whimsies go in before tabs; a cut that can't be tabbed cleanly is retried with new
    // whimsy spots (and for random, new points)
    CutTabStyle ts;
    randcut_tabstyle(rnd ? rst : randcut_style(RANDCUT_STYLE_EVEN), &ts);
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

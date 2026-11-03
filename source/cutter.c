#include "cutter.h"
#include "mazepiece.h"
#include "hexcut.h"
#include "tesscut.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

static const CutStyleInfo cutter_styles[] = {
  [CUT_STYLE_CLASSIC] = { "Classic", "classic", CUT_KIND_GRID, EDGEGEN_STYLE_CLASSIC,
      .whimsies = 1, .rotate = CUT_ROTATE_QUARTER, .samplepieces = 100 },
  [CUT_STYLE_IRREGULAR] = { "Irregular", "irregular", CUT_KIND_GRID, EDGEGEN_STYLE_IRREGULAR,
      .whimsies = 1, .rotate = CUT_ROTATE_QUARTER, .samplepieces = 100 },
  [CUT_STYLE_WILD] = { "Wild", "wild", CUT_KIND_GRID, EDGEGEN_STYLE_WILD,
      .whimsies = 1, .rotate = CUT_ROTATE_QUARTER, .samplepieces = 100 },
  [CUT_STYLE_RANDOM] = { "Random", "random", CUT_KIND_RANDOM, RANDCUT_STYLE_EVEN,
      .whimsies = 1, .rotate = CUT_ROTATE_QUARTER, .samplepieces = 100 },
  [CUT_STYLE_RANDRECT] = { "Random Rect", "randrect", CUT_KIND_RANDOM, RANDCUT_STYLE_RECT,
      .whimsies = 1, .rotate = CUT_ROTATE_QUARTER, .samplepieces = 100 },
  // Maze lines are ~6x as long: fewer sample pieces, or they cover the picture
  [CUT_STYLE_MAZE] = { "Maze", "maze", CUT_KIND_MAZE, 0,
      .whimsies = 0, .rotate = CUT_ROTATE_QUARTER, .thin = 1, .samplepieces = 24 },
  [CUT_STYLE_HEX] = { "Hex", "hex", CUT_KIND_HEX, 0,
      .whimsies = 0, .rotate = CUT_ROTATE_NONE, .samplepieces = 100 },
  // Legs and tails: grabbed by loose hulls too, packed when scattered (three orientations
  // 120 degrees apart: no quarter turns)
  [CUT_STYLE_LIZARD] = { "Lizard", "lizard", CUT_KIND_TESS, TESSCUT_SHAPE_LIZARD,
      .whimsies = 0, .rotate = CUT_ROTATE_NONE, .thin = 1, .samplepieces = 50 },
  // Quilt pinwheel stars on a 45 degree grid: one orientation, so quarter turns work. Sawtooth
  // sides are long: fewer sample pieces
  [CUT_STYLE_PINWHEEL] = { "Pinwheel", "pinwheel", CUT_KIND_TESS, TESSCUT_SHAPE_PINWHEEL,
      .whimsies = 0, .rotate = CUT_ROTATE_QUARTER, .samplepieces = 60 },
  // Swirls on a triangle grid: up and down pieces, a half turn apart. Long S sides: fewer
  // sample pieces
  [CUT_STYLE_TRIHOOK] = { "Trihook", "trihook", CUT_KIND_TESS, TESSCUT_SHAPE_TRIHOOK,
      .whimsies = 0, .rotate = CUT_ROTATE_HALF, .samplepieces = 60 },
};
// A gap in the list would be a zeroed entry: test_cut_styles checks every one is filled in
_Static_assert(sizeof cutter_styles / sizeof cutter_styles[0] == CUT_STYLES, "one CutStyleInfo per CUT_STYLE_*");

const CutStyleInfo * cutter_info(uint8_t style) {
  return cutter_styles + (style < CUT_STYLES ? style : CUT_STYLE_CLASSIC);
}

int cutter_byid(const char * id) {
  for(int st = 0; st < CUT_STYLES; st++) {
    if(!strcmp(cutter_styles[st].id, id)) return st;
  }
  return -1;
}

// Maze pieces: a cut with a repeated piece shape or a problematic piece is retried with a
// new seed
static int cutter_maze(Cutter * c, uint32_t seed) {
  c->usegraph = 1;
  for(uint32_t t = 0; t < CUTTER_MAZETRIES; t++) {
    MazePieces m;
    if(mazepiece_init(&m, c->cols, c->rows, c->count, CUTTER_MAZEAVG, CUTTER_MAZENEWEST, 0, randcut_tryseed(seed, t))) continue;
    int err = m.dups || m.problems || mazepiece_graph(&m, c->cols, c->rows, &c->g);
    mazepiece_free(&m);
    if(err) continue;
    c->g.tries = t + 1;
    c->maxpoints = cutgraph_maxpoints(&c->g);
    return 0;
  }
  return 1;
}

// Hexagons: the piece count is whatever hex grid fits closest
static int cutter_hex(Cutter * c) {
  c->usegraph = 1;
  if(hexcut_graph(c->cols, c->rows, &c->g)) return 1;
  c->g.tries = 1;
  c->count = c->g.count;
  c->maxpoints = cutgraph_maxpoints(&c->g);
  return 0;
}

// Tessellations: the piece count is whatever the border leaves near cols x rows
static int cutter_tess(Cutter * c, uint8_t shape, uint32_t seed) {
  c->usegraph = 1;
  TessVary v = { CUTTER_TESSJITTER, CUTTER_TESSBUMP, CUTTER_TESSCLEAR, CUTTER_TESSMINAREA, CUTTER_TESSNECK, 0, 0, 0 };
  if(shape == TESSCUT_SHAPE_PINWHEEL) v.jitter = v.bump = CUTTER_PINVARY;
  if(shape == TESSCUT_SHAPE_TRIHOOK) {
    v.jitter = v.bump = 0;
    v.slide = CUTTER_TRISLIDE;
  }
  TessStats st;
  if(tesscut_graph(c->cols, c->rows, tesscut_shape(shape), &v, seed, &c->g, NULL, &st)) return 1;
  c->g.tries = st.fits + st.failed;
  c->count = c->g.count;
  c->maxpoints = cutgraph_maxpoints(&c->g);
  return 0;
}

int cutter_init(Cutter * c, uint8_t style, uint8_t whimsy, uint8_t set, uint16_t cols, uint16_t rows, uint32_t seed) {
  const CutStyleInfo * si = cutter_info(style);
  memset(c, 0, sizeof(Cutter));
  c->style = style < CUT_STYLES ? style : CUT_STYLE_CLASSIC;
  c->whimsy = si->whimsies && whimsy < WHIMSY_LEVELS ? whimsy : WHIMSY_NONE;
  c->set = set < WHIMSY_SETS ? set : WHIMSY_SET_CUTE;
  c->cols = cols;
  c->rows = rows;
  c->count = (uint32_t)cols * rows;
  switch(si->kind) {
  case CUT_KIND_MAZE: return cutter_maze(c, seed);
  case CUT_KIND_HEX: return cutter_hex(c);
  case CUT_KIND_TESS: return cutter_tess(c, si->sub, seed);
  case CUT_KIND_RANDOM: return cutter_graph(c, randcut_style(si->sub), seed);
  default:
    if(c->whimsy != WHIMSY_NONE) return cutter_graph(c, NULL, seed);
    if(edgegen_init(&c->eg, cols, rows, seed, edgegen_style(si->sub))) return 1;
    c->maxpoints = EDGEGEN_MAXPOINTS;
    return 0;
  }
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
      ts.jitter = edgegen_style(cutter_info(c->style)->sub);
      if(edgegen_init(&c->eg, cols, rows, seed, ts.jitter)) return 1;
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

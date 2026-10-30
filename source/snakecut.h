#ifndef __HEADER_JIGSAW_SNAKECUT__
#define __HEADER_JIGSAW_SNAKECUT__

#include "cutgraph.h"

#include <stdint.h>

// Snake cut (experimental, Wooden4 previews): a stroke cut (strokecut, no snakes of its own)
// drawn onto a fine label grid, then snakes painted over it: thick bands leaving a piece
// straight, then hooking along an ellipse arc through whatever lies beyond (some bend into
// an S: two arcs turning opposite ways; some get a twin from the piece's far side, turned
// half round or mirrored), and now and then a yin-yang (two interleaved spiral arms from
// two neighbours).
// Snakes never cross or touch each other. Then thin bits go to their neighbours, small
// cut-off parts join a neighbour, and the smallest pieces merge until the count is right
// and none is tiny; the lines are traced and smoothed (pottscut_fromlabels). Everything in cells.

typedef struct {
  uint8_t res;          // Grid squares per cell side
  float base;           // Stroke cut pieces, times the count (merges, smallest first, bring it back)
  float snakes;         // Snakes wanted, per piece of the count
  float pairs;          // Share of them tried as yin-yangs
  float wmin, wmax;     // Snake width (cells)
  float lmin;           // Shortest snake kept (along its middle, cells)
  float lead0, lead1;   // Straight start (cells)
  float emin, emax;     // Ellipse semi-axes (cells; the tightest bend b^2/a still keeps the hook open)
  float ecc;            // Longest axis at most this times the shortest
  float sweep0, sweep1; // Hook arc turns this far (radians)
  float sbend;          // Share of snakes that bend into an S ...
  float s1sweep0, s1sweep1; // ... first arc turning this far, then a hook arc the other way
  float twin;           // Share of snakes that get a twin from the far side of their piece ...
  float mirror;         // ... mirrored (C shape) this share of the time, else turned half round (S shape)
  float taper;          // Narrows by this share toward the tip
  float gap;            // Snakes keep this far from each other and the border (cells)
  float thin;           // Bits of a piece thinner than twice this go to neighbours (cells)
  float minpart;        // A cut-off part smaller than this share of a piece joins a neighbour
  float tiny;           // After merging to the count, pieces smaller than this share still merge (base grows to make up)
  float ywmin, ywmax;   // Yin-yang band width (cells)
  float yturns0, yturns1; // Yin-yang turns
  uint8_t smooth;       // Smoothing passes on the traced lines
} SnakeStyle;

#define SNAKECUT_STYLE_DEFAULT 0
#define SNAKECUT_STYLES 1
const SnakeStyle * snakecut_style(uint8_t style);

#define SNAKECUT_TRIES 8

// Just the graph (polyline edges, no tabs): count pieces (no whimsies yet; whimsy and set
// are ignored). Same seed + style = same cut. Returns 0 on success (on failure g is empty).
int snakecut_graph(CutGraph * g, float width, float height, uint32_t count, uint8_t whimsy, uint8_t set,
    uint32_t seed, const SnakeStyle * st);

#endif

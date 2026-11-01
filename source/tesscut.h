#ifndef __HEADER_JIGSAW_TESSCUT__
#define __HEADER_JIGSAW_TESSCUT__

#include "cutgraph.h"

#include <stdint.h>

// Tessellation cuts (experiment, previews only): Escher-style tiles on a hexagon grid. Each
// tile is a flat-top hexagon whose edges are curves; edges E1, E3, E5 are E0, E2, E4 turned
// -120 degrees around the vertex they share (V1, V3, V5), so tiles come in three orientations
// 120 degrees apart and fit together everywhere. Every edge then gets its own small change
// (vertex jitter + a bump) so no two pieces are the same, rerolled where a piece gets too thin.
// Border: tiles cut to the rectangle; bits that come out small or thin join a neighbour.

// One free edge curve: points between the edge's ends, as (t along the edge, n out from the
// hexagon), both in edge lengths
typedef struct {
  uint32_t n;
  const float * tn;
} TessCurve;

// The free curves on E0 (V0 -> V1), E2 (V2 -> V3), E4 (V4 -> V5). Vertices of the unit hexagon:
// V_k = (cos 60k, sin 60k), y down (so k goes clockwise on screen).
typedef struct {
  TessCurve c[3];
} TessShape;

typedef struct {
  float jitter;   // Vertex moves, up to this x the hexagon's radius
  float bump;     // Per edge bend (sin(pi t) and sin(2 pi t) along the edge), up to this x its length
  float clear;    // Thinnest part of a piece (cells; cutgraph_thick_ok)
  float minarea;  // Border bits smaller than this (cells) join a neighbour (thin ones up to 4 x it)
  float neck;     // Border pieces narrower than this (cells) where the smaller side touches the
                  // border are cut there, the smaller side going to a neighbour (0 = off)
} TessVary;

typedef struct {
  uint32_t mismatched;  // Edges whose two tiles disagree on the curve (must be 0: shape check)
  uint32_t thinbase;    // Pieces too thin before any change (the shape itself is too thin)
  uint32_t passes;      // Reroll passes
  uint32_t flattened;   // Edges left without a bump after the passes ran out
  uint32_t merged;      // Border bits merged into a neighbour
  uint32_t fits;        // Builds until the count fit
  uint32_t failed;      // Builds that failed (retried with the lattice moved)
  uint32_t necks;       // Neck cuts made
  uint32_t neckundo;    // Neck cuts undone (would have split or pinched a piece)
} TessStats;

const TessShape * tesscut_lizard(void);
// The unit tile's outline (6 edges, closed, y down) for previews; xy holds tesscut_tilepoints
// pairs. Returns the point count.
uint32_t tesscut_tilepoints(const TessShape * sh);
uint32_t tesscut_tile(const TessShape * sh, float * xy);
// About cols x rows pieces over (0,0)-(cols,rows) (tile size fitted to the count). orient
// (malloc'd, count bytes; NULL = not wanted) gets each piece's orientation 0-2. Returns 0 on
// success.
int tesscut_graph(uint16_t cols, uint16_t rows, const TessShape * sh, const TessVary * v, uint32_t seed,
    CutGraph * g, uint8_t ** orient, TessStats * stats);

#endif

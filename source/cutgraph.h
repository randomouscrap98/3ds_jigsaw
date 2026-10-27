#ifndef __HEADER_JIGSAW_CUTGRAPH__
#define __HEADER_JIGSAW_CUTGRAPH__

#include "edgegen.h"
#include "piecegeo.h"

#include <stdint.h>

// A cut as a planar graph: vertices, edges between two pieces (or a piece and the border),
// and each piece's clockwise walk around its edges. Edges are straight, or polylines
// (whimsy outlines). Tabs sit on straight edges. Everything in cell units: the solved
// puzzle spans (0,0) to (width,height). Built by randcut (Voronoi) or from a grid (whimsy
// mode), then tabbed and checked here.

typedef struct {
  float x, y;
} CutVert;

// One tab on a straight edge: drawn over t in [pos - len/2, pos + len/2] (cells from a),
// with the tab curve's normal offsets scaled by depth (cells)
typedef struct {
  EdgeTab tab;
  float pos, len, depth;
} CutTab;

#define CUTEDGE_GRID 1      // Tabs preset from edgegen: rerolls only change the tab shape
#define CUTEDGE_WHIMSY 2    // Part of a whimsy's outline: never gets tabs

// a < b. left = the piece walking a -> b in its clockwise walk, right = the one walking
// b -> a (-1 = border). Polyline edges have npts points between a and b in pts[p0..].
typedef struct {
  uint32_t a, b;
  int32_t left, right;
  uint32_t p0, npts;
  uint8_t flags;
  uint8_t ntabs;
  uint8_t shrink;     // Times its tabs were made smaller to fit (cutgraph_tabs fallback)
  CutTab tab[2];
} CutEdge;

// How tabs are given out
typedef struct {
  float mintab;       // Edges shorter than this get no tab
  float twotab;       // Edges at least this long get two tabs
  float maxtab;       // Longest stretch of edge one tab is drawn over
  float whimsytab;    // Edges ending where a whimsy crosses get a tab only if this long
  const EdgeStyle * jitter;   // Tab shape randomness (edgegen style)
} CutTabStyle;

#define CUTGRAPH_CLEARANCE 0.05f  // Outline clearance (cells), as edgegen_shape_ok
#define CUTGRAPH_MAXPASSES 32     // Tab reroll passes before giving up
#define CUTGRAPH_FALLBACK 10      // From this pass on, stuck pieces drop or shrink tabs
#define CUTGRAPH_SHRINK 0.8f      // Tab size factor per shrink (fallback when rerolls fail)
#define CUTGRAPH_MAXSHRINK 4

typedef struct {
  uint32_t count;           // Pieces
  float width, height;
  CutVert * v;
  uint32_t nv;
  CutEdge * e;
  uint32_t ne;
  float * pts;              // Polyline points (x, y pairs)
  uint32_t npts;
  // Piece p walks pedge[pstart[p]..pstart[p+1]) clockwise; entry = edge * 2 + reversed
  uint32_t * pstart;
  uint32_t * pedge;
  uint32_t passes;          // Tab reroll passes the last cutgraph_tabs took (stats)
  uint32_t tries;           // Whole-cut attempts the builder took (stats)
} CutGraph;

// A grid cut (edgegen's corners and tabs) as a graph: piece index = row * cols + col, tabs
// preset (CUTEDGE_GRID). Outlines match edgegen_outline. Returns 0 on success.
int cutgraph_grid(CutGraph * g, const EdgeGen * eg);
void cutgraph_free(CutGraph * g);
// Deep copy (for undoing a change). Returns 0 on success.
int cutgraph_copy(CutGraph * dst, const CutGraph * src);
float cutgraph_edgelen(const CutGraph * g, const CutEdge * e);
// Give out tabs on every edge that has none yet and isn't GRID or WHIMSY preset: count by
// length, then make sure every piece except whimsies has one, then reroll tabs around
// pieces whose outline fails edgegen_shape_ok until all pass. Same seed = same tabs.
// Returns 0 on success.
int cutgraph_tabs(CutGraph * g, const CutTabStyle * st, uint32_t seed);
// Per vertex: 1 if a whimsy's outline ends there (malloc'd; NULL = out of memory)
uint8_t * cutgraph_whimsyverts(const CutGraph * g);
// 1 if edge e may get a tab under st: between two pieces, straight, not part of a whimsy,
// at least min long, and at least st->whimsytab if an end is a whimsy vertex (wv from
// cutgraph_whimsyverts; NULL when there are no whimsies)
int cutgraph_tabbable(const CutGraph * g, const CutTabStyle * st, const CutEdge * e, float min, const uint8_t * wv);
// Rebuild every piece's walk from the edges' left/right sides (after edges changed).
// Returns 0 if every piece is one closed loop; 1 if one is split, pinched or gone (g's
// walks are then unchanged), -1 out of memory.
int cutgraph_rewalk(CutGraph * g);
// Pieces that are whimsies (every edge WHIMSY) are exempt from needing a tab
int cutgraph_iswhimsy(const CutGraph * g, uint32_t p);
uint32_t cutgraph_maxpoints(const CutGraph * g);
// Closed outline of piece p, clockwise (y down), scaled by cell and offset by (ox, oy).
// xy must hold cutgraph_maxpoints pairs. Returns number of points.
uint32_t cutgraph_outline(const CutGraph * g, uint32_t p, float cell, float ox, float oy, float * xy);
// The same without tabs (the hitbox shape)
uint32_t cutgraph_body(const CutGraph * g, uint32_t p, float cell, float ox, float oy, float * xy);
// Piece geometry at cell size cell: hitbox = the piece without tabs, home center = center
// of the outline's bounding box. Returns 0 on success.
int cutgraph_geo(const CutGraph * g, float cell, PieceGeo * pg);

#endif

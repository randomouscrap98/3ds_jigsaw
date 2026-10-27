#ifndef __HEADER_JIGSAW_RANDCUT__
#define __HEADER_JIGSAW_RANDCUT__

#include "edgegen.h"
#include "piecegeo.h"

#include <stdint.h>

// Random (non-grid) cut: a Voronoi diagram of scattered points, clipped to the puzzle
// rectangle, short edges collapsed, then tabs on the edges. Pieces have 3+ sides and
// differing sizes. Everything is in cell units (average piece area = 1 cell^2) until
// scaled for output.

// How the cut looks
typedef struct {
  uint8_t relax;      // Lloyd relaxation passes: more = more even piece sizes
  float collapse;     // Edges shorter than this (cells) are collapsed to a point
  float mintab;       // Edges shorter than this get no tab
  float twotab;       // Edges at least this long get two tabs
  float maxtab;       // Longest stretch of edge one tab is drawn over (cells)
} RandCutStyle;

#define RANDCUT_STYLE_EVEN 0
#define RANDCUT_STYLES 1
const RandCutStyle * randcut_style(uint8_t style);

#define RANDCUT_CLEARANCE 0.05f   // Outline clearance (cells), as edgegen_shape_ok
#define RANDCUT_MAXPASSES 20      // Tab reroll passes before giving up on a cut
#define RANDCUT_MAXTRIES 8        // Whole-cut retries (new points) before failing

typedef struct {
  float x, y;
} RandCutVert;

// One edge between two pieces (or a piece and the border: right = -1). a < b. Tabs sit on
// stretches of the edge: tab k covers t in [pos[k] - len[k]/2, pos[k] + len[k]/2] (t in
// cells from a) and is scaled by len[k].
typedef struct {
  uint32_t a, b;
  int32_t left, right;  // Pieces on each side; left = the one walking a -> b clockwise
  uint8_t ntabs;
  EdgeTab tab[2];
  float pos[2], len[2];
} RandCutEdge;

typedef struct {
  uint32_t count;           // Pieces
  float width, height;      // Puzzle size (cells)
  RandCutVert * v;
  uint32_t nv;
  RandCutEdge * e;
  uint32_t ne;
  // Piece p walks pedge[pstart[p]..pstart[p+1]) clockwise; entry = edge * 2 + reversed
  uint32_t * pstart;
  uint32_t * pedge;
  uint32_t tries;           // Whole-cut attempts it took (stats)
  uint32_t passes;          // Tab reroll passes the last attempt took (stats)
} RandCut;

// Cut a width x height (cells) puzzle into count pieces. Same seed + style = same cut.
// Returns 0 on success.
int randcut_init(RandCut * c, float width, float height, uint32_t count, uint32_t seed, const RandCutStyle * st);
void randcut_free(RandCut * c);
// Closed outline of piece p, clockwise (y down), scaled by cell and offset by (ox, oy).
// xy must hold randcut_maxpoints pairs. Returns number of points.
uint32_t randcut_maxpoints(const RandCut * c);
uint32_t randcut_outline(const RandCut * c, uint32_t p, float cell, float ox, float oy, float * xy);
// Piece geometry at cell size cell: hitbox = the piece's corner polygon (no tabs), home
// center = center of the outline's bounding box. Returns 0 on success.
int randcut_geo(const RandCut * c, float cell, PieceGeo * g);

#endif

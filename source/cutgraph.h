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

#include "knobs.h"

// Where curls and swirls go (knobs.h has their shapes)
#define CUTGRAPH_CURL_MINEDGE 0.8f  // Only edges this long (cells) get curls
#ifndef CUTGRAPH_SWIRL_SPAN
#define CUTGRAPH_SWIRL_SPAN 0.7f      // Swept: most swirls that keep their pieces thick enough
#endif
#ifndef CUTGRAPH_SWIRL_CLEAR
#define CUTGRAPH_SWIRL_CLEAR 0.07f    // Thinnest part of a piece with a swirl (cells)
#endif
#ifndef CUTGRAPH_SWIRL_MINEDGE
#define CUTGRAPH_SWIRL_MINEDGE 0.9f // Only edges this long (cells) get swirls
#endif

// One tab on a straight edge: drawn over t in [pos - len/2, pos + len/2] (cells from a),
// with the tab curve's normal offsets scaled by depth (cells)
typedef struct {
  EdgeTab tab;
  float pos, len, depth;
  uint8_t kind;       // CUTKNOB_*
} CutTab;

#define CUTEDGE_GRID 1      // Tabs preset from edgegen: rerolls only change the tab shape
#define CUTEDGE_WHIMSY 2    // Part of a whimsy's outline: never gets tabs
#define CUTEDGE_NOCURL 4    // A curl here didn't fit: plain knobs only from now on
#define CUTEDGE_SNAKE 8     // A snake (strokecut): never gets tabs, and its pieces need none

// a < b. left = the piece walking a -> b in its clockwise walk, right = the one walking
// b -> a (-1 = border). Polyline edges have npts points between a and b in pts[p0..].
// A straight edge can wave sideways: at s cells from a (chord length L, u = pi s / L) its
// line is pushed along the normal (-uy, ux) by L sin^2(u) (wave[0] + wave[1] cos(u)): a bump
// and an S, both leaving the corners straight (no sharp points there); tabs follow the line.
typedef struct {
  uint32_t a, b;
  int32_t left, right;
  uint32_t p0, npts;
  uint8_t flags;
  uint8_t ntabs;
  uint8_t shrink;     // Times its tabs were made smaller to fit (cutgraph_tabs fallback)
  CutTab tab[2];
  float wave[2];      // 0, 0 = straight
  // Stroke curve (instead of a wave): a cubic leaving a along unit direction dir[0..1] and b
  // along dir[2..3] (both pointing into the edge), with tangent lengths bend * L. 0 = straight.
  float dir[4];
  float bend;
} CutEdge;

// How tabs are given out
typedef struct {
  float mintab;       // Edges shorter than this get no tab
  float twotab;       // Edges at least this long get two tabs
  float maxtab;       // Longest stretch of edge one tab is drawn over
  float whimsytab;    // Edges ending where a whimsy crosses get a tab only if this long
  const EdgeStyle * jitter;   // Tab shape randomness (edgegen style)
  float wave;         // Inner edges wave up to this much (wave[0]; fraction of length; 0 = straight)
  uint8_t knobs;      // Knob kinds used: 0 or 1 = classic only; n > 1 = CUTKNOB_* 1 .. n-1 (no
                      // classic: its head sits low, leaving a thin sliver under it)
  float curls;        // Share of knobs that are curls instead (0 = none)
  float swirls;       // Share of knobs (on long enough edges) that are swirls (0 = none)
  float bend;         // Stroke curves (directions from cornerbend_dirs; 0 = off): edges bend up to this (tangent
                      // length / edge length), rolled with the tabs
  float knoblen;      // Tabs span this many cells (+-CUTGRAPH_KNOBVARY; 0 = sized to the edge)
  float knobsize;     // Head size (edgegen tab size; 0 = edgegen's), shape jitter halved
  uint8_t onekind;    // Every tab this CUTKNOB_* (0 = as knobs says)
  float fallbacktab;  // A piece with no tab gets one on its longest edge if at least this long
                      // (0 = 0.6 * mintab)
  float clearance;    // Thinnest part of a piece (cells; 0 = CUTGRAPH_CLEARANCE). When set, also
                      // checked by cutgraph_thick_ok (no pinches next to corners, no sharp points)
} CutTabStyle;

#define CUTGRAPH_CLEARANCE 0.05f  // Outline clearance (cells), as edgegen_shape_ok
#define CUTGRAPH_MAXPASSES 32     // Tab reroll passes before giving up
#define CUTGRAPH_FALLBACK 10      // From this pass on, stuck pieces drop or shrink tabs
#define CUTGRAPH_CURLFALLBACK 4   // From this pass on, stuck pieces' curls become plain knobs
#define CUTGRAPH_SHRINK 0.8f      // Tab size factor per shrink (fallback when rerolls fail)
#define CUTGRAPH_MAXSHRINK 4
#define CUTGRAPH_WAVESEG 10       // Line segments along a waved edge
#define CUTGRAPH_WAVEBODY 4       // ... in hitboxes (cutgraph_body): only roughly the shape
#define CUTGRAPH_BENDMIN 0.6f     // Stroke edges bend at least this much of the style's bend
#define CUTGRAPH_KNOBVARY 0.1f    // knoblen / knobsize vary by this share
#ifndef CUTGRAPH_KNOBSIZEVARY
#define CUTGRAPH_KNOBSIZEVARY CUTGRAPH_KNOBVARY  // knobsize alone (overridable for experiments)
#endif
#define CUTGRAPH_WAVEMIN 0.5f     // Shorter edges stay straight (a wave on them is a tight wiggle)
#define CUTGRAPH_WAVEKNOB 0.25f   // An edge with one knob bumps (wave[0]) only this much of the style's wave
#define CUTGRAPH_WAVETWO 0.3f     // An edge with two knobs waves only this much

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
#define CUTGRAPH_PINCH 3.0f       // cutgraph_thick_ok: outline points this many cl apart along it must be cl apart
// 1 if no two points of closed outline xy (n points) are closer than cl while more than
// CUTGRAPH_PINCH * cl apart along the outline: no thin necks or slivers anywhere (unlike
// edgegen_shape_ok, which skips nearby points by count), and no corners sharper than ~40°
int cutgraph_thick_ok(const float * xy, uint32_t n, float cl);
// Closed outline of piece p, clockwise (y down), scaled by cell and offset by (ox, oy).
// xy must hold cutgraph_maxpoints pairs. Returns number of points.
uint32_t cutgraph_outline(const CutGraph * g, uint32_t p, float cell, float ox, float oy, float * xy);
// The same without tabs (the hitbox shape)
uint32_t cutgraph_body(const CutGraph * g, uint32_t p, float cell, float ox, float oy, float * xy);
// Piece geometry at cell size cell: hitbox = the piece without tabs, home center = center
// of the outline's bounding box. Returns 0 on success.
int cutgraph_geo(const CutGraph * g, float cell, PieceGeo * pg);

#endif

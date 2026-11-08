#ifndef __HEADER_JIGSAW_CUTTER__
#define __HEADER_JIGSAW_CUTTER__

#include "edgegen.h"
#include "randcut.h"
#include "cutgraph.h"
#include "whimsy.h"
#include "piecegeo.h"
#include "progress.h"

#include <stdint.h>

// Cut styles as stored in saves, cache names and the menu. What each one is and how it
// plays is in its CutStyleInfo (cutter_info); code asks that instead of checking style
// numbers. (The shelved wooden experiments live in woodcut, numbered from CUT_STYLES.)
#define CUT_STYLE_CLASSIC 0
#define CUT_STYLE_IRREGULAR 1
#define CUT_STYLE_WILD 2
#define CUT_STYLE_RANDOM 3
#define CUT_STYLE_RANDRECT 4
#define CUT_STYLE_MAZE 5
#define CUT_STYLE_HEX 6
#define CUT_STYLE_LIZARD 7
#define CUT_STYLE_PINWHEEL 8
#define CUT_STYLE_TRIHOOK 9
#define CUT_STYLES 10

// Which builder cuts a style (CutStyleInfo.kind)
#define CUT_KIND_GRID 0     // edgegen grid, sub = EDGEGEN_STYLE_* (a CutGraph when it has whimsies)
#define CUT_KIND_RANDOM 1   // randcut (Voronoi), sub = RANDCUT_STYLE_*
#define CUT_KIND_MAZE 2     // mazepiece
#define CUT_KIND_HEX 3      // hexcut
#define CUT_KIND_TESS 4     // tesscut, sub = TESSCUT_SHAPE_*

#define CUT_ROTATE_NONE 0
#define CUT_ROTATE_QUARTER 1
#define CUT_ROTATE_HALF 2   // Up and down pieces swap (Trihook)

typedef struct {
  const char * name;      // Menu name
  const char * id;        // Short lowercase name: tools, tests, preview file names
  uint8_t kind, sub;      // CUT_KIND_* and that builder's own style
  uint8_t whimsies;       // Can have whimsies (otherwise the setting is ignored and grayed out)
  uint8_t rotate;         // CUT_ROTATE_*: how pieces turn (a quarter turned hex doesn't fit its grid)
  uint8_t hulls;          // Pieces with narrow parts or hollows: grabbed by loose hulls too
  uint8_t sprawl;         // Bounds mostly empty (maze, lizard): scattered by packing their bounds
                          // on a bigger table, condensed by outline
  uint8_t samplepieces;   // Menu cut sample size (long winding lines need fewer)
  uint16_t maxpieces;     // Biggest piece count the menu offers (cut time, and Maze fails at 2000)
} CutStyleInfo;

// A style's description; unknown styles get Classic's (as cutter_init cuts them)
const CutStyleInfo * cutter_info(uint8_t style);
// Style with that id, or -1
int cutter_byid(const char * id);

#define CUTTER_MAXTRIES 16  // Whimsy attempts (new seeds) before cutting without whimsies
#define CUTTER_MAZEAVG 20   // Maze pieces: cells per piece (complexity vs arm width)
#define CUTTER_MAZENEWEST 0.5f  // Maze branchiness (mazepiece_init newest)
#define CUTTER_MAZETRIES 32 // Maze attempts (new seeds) for one with no repeated shapes
// Tessellation cuts (tesscut): per-edge change (user picked 0.08), thinnest part, border bits
// that join a neighbour, necks cut at the border (lizard legs are ~0.13 cells wide)
#define CUTTER_TESSJITTER 0.08f
#define CUTTER_TESSBUMP 0.08f
#define CUTTER_PINVARY 0.12f   // Pinwheel jitter and bump (stronger: the stars still read as stars)
#define CUTTER_TRISLIDE 0.15f  // Trihook: only slides (each S's middle moves along its side)
#define CUTTER_TESSCLEAR 0.06f
#define CUTTER_TESSMINAREA 0.4f
#define CUTTER_TESSNECK 0.2f

// One puzzle's cut, whatever the style: piece outlines and geometry for a cols x rows
// puzzle (in cells; the solved puzzle spans (0,0) to (cols,rows)). Grid styles without
// whimsies use edgegen directly (pieces exactly as in 1.0-1.1); everything else is a
// CutGraph. Pieces 0 .. cols*rows-1 are the grid/random pieces, whimsies come after.
typedef struct {
  uint8_t style, whimsy, set;
  uint16_t cols, rows;
  uint32_t count;       // Pieces (including whimsies)
  uint32_t whimsies;    // Whimsies placed
  uint32_t maxpoints;   // Largest outline (points)
  uint8_t usegraph;
  EdgeGen eg;           // Grid styles
  CutGraph g;           // Random style, or anything with whimsies
} Cutter;

// Same style + whimsy level + set + seed = same cut. Unknown styles cut classic, unknown
// whimsy levels/sets are none/cute. Returns 0 on success.
int cutter_init(Cutter * c, uint8_t style, uint8_t whimsy, uint8_t set, uint16_t cols, uint16_t rows, uint32_t seed);
// cutter_init with progress reports for the slow parts (pr may be NULL): "Cutting pieces" in random and
// tessellation cuts, "Adding whimsies"
int cutter_initp(Cutter * c, uint8_t style, uint8_t whimsy, uint8_t set, uint16_t cols, uint16_t rows, uint32_t seed,
    const Progress * pr);
void cutter_free(Cutter * c);
// For cutter_init and woodcut: with style, whimsy, set, cols, rows and count filled in,
// builds c->g (usegraph): a random cut with rst, or a grid (c->style's edgegen style) with
// whimsies when rst is NULL. Sets whimsies, count and maxpoints. Returns 0 on success. pr may be NULL.
int cutter_graph(Cutter * c, const RandCutStyle * rst, uint32_t seed, const Progress * pr);
// Closed outline of piece p (clockwise, y down) in the puzzle frame scaled by cell, then
// offset by (ox, oy). xy must hold maxpoints pairs. Returns the number of points.
uint32_t cutter_outline(const Cutter * c, uint32_t p, float cell, float ox, float oy, float * xy);
// Bounds of every piece's outline in cells: count x (x0, y0, x1, y1). Returns 0 on success.
int cutter_bounds(const Cutter * c, float * boxes);
// Fingerprint of the cut: every outline in cells, rounded to 1/1000 (so it doesn't depend on
// the piece size in pixels or tiny float differences). Saves keep it to notice a cut that
// changed under them; tests pin it per style. Never 0.
uint32_t cutter_hash(const Cutter * c);
// Piece geometry at cell size cell (home centers = cell/region centers; callers that pack
// tiles move them to the tile centers). Returns 0 on success.
int cutter_geo(const Cutter * c, float cell, PieceGeo * g);

#endif

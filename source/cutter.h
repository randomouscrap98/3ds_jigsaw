#ifndef __HEADER_JIGSAW_CUTTER__
#define __HEADER_JIGSAW_CUTTER__

#include "edgegen.h"
#include "randcut.h"
#include "cutgraph.h"
#include "whimsy.h"
#include "piecegeo.h"

#include <stdint.h>

// Cut styles as stored in saves, cache names and the menu. 0-2 are grid cuts (edgegen
// styles), 3-5 are random (Voronoi) cuts: scattered points, points on a jittered grid, or
// scattered points with waved edges and mixed knobs (wooden).
#define CUT_STYLE_CLASSIC 0
#define CUT_STYLE_IRREGULAR 1
#define CUT_STYLE_WILD 2
#define CUT_STYLE_RANDOM 3
#define CUT_STYLE_RANDRECT 4
#define CUT_STYLE_WOODEN 5      // Not in the menu yet (previews)
#define CUT_STYLE_WOODEN2 6     // Not in the menu yet (previews): stroke cut (strokecut) + snakes
#define CUT_STYLE_WOODEN3 7     // Not in the menu yet (previews): cellular Potts cut (pottscut)
#define CUT_STYLE_WOODEN4 8     // Not in the menu yet (previews): stroke cut + painted snakes (snakecut)
#define CUT_STYLE_WOODEN5 9     // Not in the menu yet (previews): stroke cut + T-junction snakes (snakecut) + Wooden2 knobs
#define CUT_STYLES 10

#define CUTTER_MAXTRIES 16  // Whimsy attempts (new seeds) before cutting without whimsies

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
void cutter_free(Cutter * c);
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

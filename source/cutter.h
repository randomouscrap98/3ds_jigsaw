#ifndef __HEADER_JIGSAW_CUTTER__
#define __HEADER_JIGSAW_CUTTER__

#include "edgegen.h"
#include "randcut.h"
#include "piecegeo.h"

#include <stdint.h>

// Cut styles as stored in saves, cache names and the menu. 0-2 are grid cuts (edgegen
// styles), 3 is the random (Voronoi) cut.
#define CUT_STYLE_CLASSIC 0
#define CUT_STYLE_IRREGULAR 1
#define CUT_STYLE_WILD 2
#define CUT_STYLE_RANDOM 3
#define CUT_STYLES 4

// One puzzle's cut, whatever the style: piece outlines and geometry for a cols x rows
// puzzle (in cells; the solved puzzle spans (0,0) to (cols,rows)).
typedef struct {
  uint8_t style;
  uint16_t cols, rows;
  uint32_t count;       // Pieces
  uint32_t maxpoints;   // Largest outline (points)
  EdgeGen eg;           // Grid styles
  RandCut rc;           // Random style
} Cutter;

// Same style + seed = same cut. Unknown styles cut classic. Returns 0 on success.
int cutter_init(Cutter * c, uint8_t style, uint16_t cols, uint16_t rows, uint32_t seed);
void cutter_free(Cutter * c);
// Closed outline of piece p (clockwise, y down) in the puzzle frame scaled by cell, then
// offset by (ox, oy). xy must hold maxpoints pairs. Returns the number of points.
uint32_t cutter_outline(const Cutter * c, uint32_t p, float cell, float ox, float oy, float * xy);
// Bounds of every piece's outline in cells: count x (x0, y0, x1, y1). Returns 0 on success.
int cutter_bounds(const Cutter * c, float * boxes);
// Piece geometry at cell size cell (home centers = cell/region centers; callers that pack
// tiles move them to the tile centers). Returns 0 on success.
int cutter_geo(const Cutter * c, float cell, PieceGeo * g);

#endif

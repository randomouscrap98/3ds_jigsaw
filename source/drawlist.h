#ifndef __HEADER_JIGSAW_DRAWLIST__
#define __HEADER_JIGSAW_DRAWLIST__

#include <stdint.h>

#include "board.h"

#define DRAWLIST_MAXGRID 64   // Overlap grid is at most this many squares across

// The order to draw pieces in, arranged so pieces on the same texture page come together
// (each page change splits the GPU batch, which is expensive on the CPU).
// Draw order only matters between groups that overlap, so groups are put in layers: a
// group goes one layer above the highest earlier group whose bounding box it touches.
// Each layer is drawn page by page. The top ntop groups of the board order are "moving"
// (held / dragged selection) and always go above everything else, so the list stays
// valid while they move. Rebuild after anything that changes the board order or moves
// a non-moving group, or changes moving groups relative to each other.
typedef struct {
  uint16_t * pieces;    // Draw order, bottom first (count entries)
  uint32_t count;
  uint32_t layers;      // Layers used by the last build
  uint8_t nogrid;       // Testing: compare every pair of groups instead of using the grid
  // Scratch
  float * box;          // Per group slot: x0, y0, x1, y1
  uint16_t * layer;     // Per group slot
  uint32_t * key;       // Per piece: layer * pages + page
} DrawList;

int drawlist_init(DrawList * d, uint32_t count);
void drawlist_free(DrawList * d);
// pad = how far a piece's drawing reaches from its center (half the sprite size), world
// units. page = texture page of every piece (each < pages).
void drawlist_build(DrawList * d, Board * b, float pad, const uint8_t * page, uint32_t pages, uint32_t ntop);

#endif

#ifndef __HEADER_JIGSAW_LAYOUT__
#define __HEADER_JIGSAW_LAYOUT__

#include <stdint.h>

#define LAYOUT_PAGESIZE 1024
// Room around a cell for tabs (reach, in cells) plus antialiasing/outline pixels
#define LAYOUT_MARGIN(cell, reach) ((uint16_t)((cell) * (reach) + 0.999f) + 2)
#define LAYOUT_MINCELL 8

// How a puzzle is cut and packed: grid size, piece size in pixels, which part of the
// source image is used, and where each piece's tile lives in the texture pages.
// Each tile is the cell plus a margin on every side for tabs, so the piece center is
// always the tile center.
typedef struct {
  uint16_t cols, rows;
  uint16_t cell;      // Cell size in pixels (of the scaled puzzle image)
  uint16_t margin;    // Extra pixels around the cell in each tile
  uint16_t tile;      // cell + 2 * margin
  uint16_t perrow;    // Tiles per page row
  uint16_t perpage;
  uint16_t pages;
  uint32_t width, height;           // Scaled puzzle image size = cols*cell x rows*cell
  float cropx, cropy, cropw, croph; // Region of the source image used (centered crop)
} Layout;

// Choose a grid close to target pieces that matches the image aspect, then the biggest
// cell size that fits maxside and maxpages. reach = how far (in cells) pieces can stick
// out of their cell (see edgegen). Never upscales. Returns 0 on success.
int layout_compute(Layout * l, uint32_t srcw, uint32_t srch, uint32_t target,
    uint32_t maxside, uint32_t maxpages, float reach);
// Where piece's tile is: page index and top-left pixel in that page
void layout_tile(const Layout * l, uint32_t piece, uint16_t * page, uint16_t * x, uint16_t * y);
// Just the grid choice (exposed for testing)
void layout_grid(uint32_t srcw, uint32_t srch, uint32_t target, uint16_t * cols, uint16_t * rows);

#endif

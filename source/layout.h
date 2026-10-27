#ifndef __HEADER_JIGSAW_LAYOUT__
#define __HEADER_JIGSAW_LAYOUT__

#include <stdint.h>

#define LAYOUT_PAGESIZE 1024
#define LAYOUT_PAD 2          // Pixels around each piece's outline in its tile (antialiasing)
#define LAYOUT_MINCELL 8

// How a puzzle is cut and packed: grid size, piece size in pixels, which part of the
// source image is used. Where each piece's tile lives is in a separate LayoutTile array.
typedef struct {
  uint16_t cols, rows;
  uint16_t cell;      // Cell size in pixels (of the scaled puzzle image)
  uint16_t pages;     // Texture pages used
  uint16_t maxw, maxh;              // Biggest tile
  uint32_t width, height;           // Scaled puzzle image size = cols*cell x rows*cell
  float cropx, cropy, cropw, croph; // Region of the source image used (centered crop)
} Layout;

// One piece's tile: its rectangle in a texture page, and the image pixel its top-left
// corner shows (can be negative: tiles reach past the image edge). The piece's home
// center is the tile's center: (ox + w / 2, oy + h / 2).
typedef struct {
  uint16_t page, x, y, w, h;
  int32_t ox, oy;
} LayoutTile;

// Choose a grid close to target pieces that matches the image aspect, and the centered
// crop of the source image with that aspect. Returns 0 on success.
int layout_crop(Layout * l, uint32_t srcw, uint32_t srch, uint32_t target);
// Pick the biggest cell size (never upscaling the crop, at most maxside pixels per side)
// whose tiles pack into maxpages pages, and fill in tiles. boxes: count x (x0, y0, x1, y1),
// each piece's outline bounds in cells. Shelf packing, tallest first: deterministic.
// Returns 0 on success, 1 out of memory, 2 if even LAYOUT_MINCELL doesn't fit.
int layout_fit(Layout * l, const float * boxes, uint32_t count, uint32_t maxside, uint32_t maxpages,
    LayoutTile * tiles);
// Just the grid choice (exposed for testing)
void layout_grid(uint32_t srcw, uint32_t srch, uint32_t target, uint16_t * cols, uint16_t * rows);

#endif

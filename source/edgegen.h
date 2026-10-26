#ifndef __HEADER_JIGSAW_EDGEGEN__
#define __HEADER_JIGSAW_EDGEGEN__

#include <stdint.h>

#define EDGEGEN_SEGMENTS 8   // Line segments per bezier curve (3 curves per tab)
#define EDGEGEN_MAXPOINTS (4 * 3 * EDGEGEN_SEGMENTS)

// Shape of one internal edge: a classic tab, made of 3 cubic beziers in edge space
// (t along the edge 0..1, n across it, in cell units). flip = +1/-1 picks which side
// the tab sticks out to. t is tab size, the rest are small jitters.
typedef struct {
  float size, a, b, c, d, e;
  int8_t flip;
} EdgeTab;

// All the internal edges of a cols x rows grid, generated from a seed. The same seed
// always produces the same cut.
//   h: horizontal edges, the one below cell (c, r) is h[r * cols + c], r < rows - 1
//   v: vertical edges, the one right of cell (c, r) is v[r * (cols - 1) + c], c < cols - 1
//   corners: (cols + 1) x (rows + 1) grid corner offsets (dx, dy pairs, in cells).
//     All zero for a classic straight grid; jittered for an irregular cut. Corners on the
//     outer border only slide along it, so the puzzle stays a rectangle.
typedef struct {
  EdgeTab * h;
  EdgeTab * v;
  float * corners;
  uint16_t cols, rows;
} EdgeGen;

// Tabs stick out at most this far (in cells) from their edge line
#define EDGEGEN_TABREACH 0.35f
// Corner jitter for irregular cuts, in cells (pass to edgegen_init / layout)
#define EDGEGEN_IRREGULAR 0.1f

// cornerjitter = 0 gives a classic straight grid; EDGEGEN_IRREGULAR gives an irregular one.
// Pieces never extend more than EDGEGEN_TABREACH + cornerjitter cells outside their cell.
int edgegen_init(EdgeGen * eg, uint16_t cols, uint16_t rows, uint32_t seed, float cornerjitter);
void edgegen_free(EdgeGen * eg);
// Closed outline of piece (col, row), clockwise (y down), in coordinates where the
// piece's cell spans (ox, oy) to (ox + cell, oy + cell). xy must hold EDGEGEN_MAXPOINTS
// pairs. Returns number of points. Neighbouring pieces' outlines share edges exactly.
int edgegen_outline(const EdgeGen * eg, uint16_t col, uint16_t row, float cell,
    float ox, float oy, float * xy);

#endif

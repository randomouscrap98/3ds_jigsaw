#ifndef __HEADER_JIGSAW_EDGEGEN__
#define __HEADER_JIGSAW_EDGEGEN__

#include "rng.h"

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
//   style: how wild the cut is (see EdgeStyle)
typedef struct {
  float corner;     // Grid corner jitter (0 = straight grid)
  float tabsize;    // Tab size jitter
  float shape;      // Tab shape jitter (position, skew, edge wobble)
  float reach;      // Pieces never stick out of their cell further than this (enforced)
} EdgeStyle;

typedef struct {
  EdgeTab * h;
  EdgeTab * v;
  float * corners;
  uint16_t cols, rows;
  EdgeStyle style;
} EdgeGen;

#define EDGEGEN_STYLE_CLASSIC 0
#define EDGEGEN_STYLE_IRREGULAR 1
#define EDGEGEN_STYLE_WILD 2
#define EDGEGEN_STYLES 3

// Every piece outline is checked after generation: it may not cross itself, come closer
// than this (in cells) to another part of itself, or stick out past the style's reach.
// Pieces that fail get their edges and corners rerolled (deterministically from the seed).
#define EDGEGEN_CLEARANCE 0.06f
#define EDGEGEN_CLEARSKIP 12    // Outline points this close along the outline aren't checked for clearance
#define EDGEGEN_MAXPASSES 20

// Settings for EDGEGEN_STYLE_* (unknown values give classic)
const EdgeStyle * edgegen_style(uint8_t style);
// The same seed and style always produce the same cut. Returns 0 on success.
int edgegen_init(EdgeGen * eg, uint16_t cols, uint16_t rows, uint32_t seed, const EdgeStyle * style);
void edgegen_free(EdgeGen * eg);
// 1 if the piece's outline passes the checks above
int edgegen_piece_ok(const EdgeGen * eg, uint16_t col, uint16_t row);
// 1 if closed outline xy (n points) doesn't cross itself or come closer than cl to another
// part of itself (points within EDGEGEN_CLEARSKIP along the outline aren't checked)
int edgegen_shape_ok(const float * xy, int n, float cl);

// Building blocks for other cuts (randcut). Random tab shape for a style:
void edgegen_randomtab(Rng * r, const EdgeStyle * st, EdgeTab * t);
// Sample a tab (NULL = straight) in edge space: out gets 3 * EDGEGEN_SEGMENTS + 1 (t, n)
// pairs from t = 0 to t = 1 (n in edge-length units, flip applied). Returns the count.
int edgegen_tabcurve(const EdgeTab * tab, float * out);
// Closed outline of piece (col, row), clockwise (y down), in coordinates where the
// piece's cell spans (ox, oy) to (ox + cell, oy + cell). xy must hold EDGEGEN_MAXPOINTS
// pairs. Returns number of points. Neighbouring pieces' outlines share edges exactly.
int edgegen_outline(const EdgeGen * eg, uint16_t col, uint16_t row, float cell,
    float ox, float oy, float * xy);

#endif

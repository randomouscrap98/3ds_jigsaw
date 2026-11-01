#ifndef __HEADER_JIGSAW_HEXCUT__
#define __HEADER_JIGSAW_HEXCUT__

#include "cutgraph.h"

#include <stdint.h>

// Hex cut: identical flat-top hexagons in columns, no tabs. The left/right borders run
// through the first and last columns' centers and the top/bottom borders through the even
// columns' centers, so every edge piece is a half hexagon (corners: quarters) and odd
// columns end flush with a full hexagon. Hexes stretch a little to fit the picture.

// Hex columns and rows (rows = even columns' center spacings top to bottom) for a cols x
// rows puzzle: piece count near cols * rows, hexes near regular
void hexcut_size(uint16_t cols, uint16_t rows, uint16_t * hcols, uint16_t * hrows);
// Pieces in that hex grid
uint32_t hexcut_count(uint16_t hcols, uint16_t hrows);
// The cut as a graph spanning (0,0) to (cols,rows): straight edges, no tabs. Pieces go
// column by column, top to bottom. Returns 0 on success.
int hexcut_graph(uint16_t cols, uint16_t rows, CutGraph * g);

#endif

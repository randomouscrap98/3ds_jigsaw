#ifndef __HEADER_JIGSAW_MAZEPIECE__
#define __HEADER_JIGSAW_MAZEPIECE__

#include "cutgraph.h"

#include <stdint.h>

// Maze pieces (experiment, previews only): a block maze (walls are full cells, outer wall
// cut off) on a w x h cell grid. Corridors form one tree, walls a forest reaching the edge;
// each is cut into connected pieces of about the same size, so every piece is a root one
// cell wide, wrapped around its neighbours' (corridor pieces between wall pieces).
typedef struct {
  uint16_t w, h;        // Cells (both odd: rooms on even x and y)
  uint8_t * wall;       // w*h: 1 = wall cell
  int32_t * label;      // w*h: piece of each cell
  uint32_t count;       // Pieces
  uint32_t thr;         // Cut threshold used (cells)
  uint32_t tiny;        // Components too small for a piece, merged into a neighbour
  uint32_t flips;       // Connectors flipped to join stubs to a bigger branch
  uint32_t cut;         // Pieces before splitting/merging to the exact count
  uint32_t dups;        // Pieces whose shape repeats another's (turns count, mirror images don't)
} MazePieces;

// Cut a cols x rows puzzle (in pieces) into exactly count pieces of about avg cells each.
// newest = maze branchiness: 1 = depth-first (long corridors), 0 = Prim-like (bushy).
// Returns 0 on success.
int mazepiece_init(MazePieces * m, uint16_t cols, uint16_t rows, uint32_t count, uint32_t avg, float newest, uint32_t seed);
void mazepiece_free(MazePieces * m);
// The pieces as a cut graph for a cols x rows puzzle: straight edges along the cell
// borders (vertices where lines meet or turn), no tabs. Returns 0 on success.
int mazepiece_graph(const MazePieces * m, uint16_t cols, uint16_t rows, CutGraph * g);

#endif

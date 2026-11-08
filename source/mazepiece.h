#ifndef __HEADER_JIGSAW_MAZEPIECE__
#define __HEADER_JIGSAW_MAZEPIECE__

#include "cutgraph.h"
#include "progress.h"

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
  uint32_t problems;    // Problematic pieces (mazepiece_problems)
  uint32_t capsplits;   // Splits done for the size cap
  uint32_t minmerges;   // Small pieces merged (capped only)
} MazePieces;

// Cut a cols x rows puzzle (in pieces) into exactly count pieces of about avg cells each.
// newest = maze branchiness: 1 = depth-first (long corridors), 0 = Prim-like (bushy).
// cap > 0: pieces over cap x the mean size are split (best effort). Returns 0 on success.
// pr (may be NULL) hears PROGRESS_CUTTING through the threshold scan.
int mazepiece_init(MazePieces * m, uint16_t cols, uint16_t rows, uint32_t count, uint32_t avg, float newest, float cap, uint32_t seed,
    const Progress * pr);
void mazepiece_free(MazePieces * m);
// Problematic pieces (either hand): exactly a 4-way cell whose four arms all turn the
// same way, each arm and hook straight, nothing else in the piece. Uses w, h, label, count.
uint32_t mazepiece_problems(const MazePieces * m);
// The pieces as a cut graph for a cols x rows puzzle: straight edges along the cell
// borders (vertices where lines meet or turn), no tabs. Returns 0 on success.
int mazepiece_graph(const MazePieces * m, uint16_t cols, uint16_t rows, CutGraph * g);

#endif

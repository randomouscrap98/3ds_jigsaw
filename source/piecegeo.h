#ifndef __HEADER_JIGSAW_PIECEGEO__
#define __HEADER_JIGSAW_PIECEGEO__

#include <stdint.h>

// Where every piece belongs in the solved puzzle and who it touches: home centers,
// neighbour lists, coarse hitbox polygons and a lookup grid for finding the piece under a
// point. All in the puzzle frame: the solved puzzle spans (0,0) to (width,height), in the
// same units as the board. Never saved; rebuilt from the cut on every load.
typedef struct {
  uint32_t count;       // Pieces
  uint16_t cols, rows;  // Grid the cut was made from
  float cell;           // Typical piece size (snap distance, glow, zoom)
  float width, height;  // Solved puzzle size
  float * center;       // count x (x, y): home center of each piece (its tile center)
  uint32_t * nbstart;   // count + 1: piece p's neighbours are nb[nbstart[p]..nbstart[p+1])
  uint16_t * nb;
  uint32_t * hbstart;   // count + 1: piece p's hitbox is points hb[hbstart[p]..hbstart[p+1])
  float * hb;           // Hitbox points (x, y), a simple polygon per piece
  // Lookup grid: square ls x ls, lw x lh of them; square i lists the pieces whose
  // hitbox bounds touch it in lpiece[lstart[i]..lstart[i+1])
  float ls;
  uint16_t lw, lh;
  uint32_t * lstart;
  uint16_t * lpiece;
} PieceGeo;

// Straight grid of square cells (the classic layout). Piece index = row * cols + col,
// hitbox = the cell, neighbours = the 4 adjacent cells (up, down, left, right).
// Returns 0 on success.
int piecegeo_grid(PieceGeo * g, uint16_t cols, uint16_t rows, float cell);
void piecegeo_free(PieceGeo * g);
// Piece whose hitbox contains puzzle-frame point (x, y), or -1. Hitboxes are half-open
// (a point on a shared edge belongs to one piece only).
int32_t piecegeo_at(const PieceGeo * g, float x, float y);

#endif

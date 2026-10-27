#ifndef __HEADER_JIGSAW_PIECEGEO__
#define __HEADER_JIGSAW_PIECEGEO__

#include <stddef.h>
#include <stdint.h>

#define PIECEGEO_VERSION 1

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

// Grid of cells (edgegen's layout). Piece index = row * cols + col, home center = the
// cell's center, neighbours = the 4 adjacent cells (up, down, left, right), hitbox = the
// quad through the cell's 4 corners. corners: (cols + 1) x (rows + 1) corner offsets
// (dx, dy pairs, in cells) as in EdgeGen, or NULL for a straight grid of squares.
// Returns 0 on success.
int piecegeo_grid(PieceGeo * g, uint16_t cols, uint16_t rows, float cell, const float * corners);
void piecegeo_free(PieceGeo * g);
// PieceGeo <-> flat byte buffer, little endian, for the piece cache (the lookup grid is
// rebuilt on read, not stored). Layout:
//   "PGEO" u16 version, u16 cols, u16 rows, f32 cell, f32 width, f32 height,
//   u32 count, u32 neighbour entries, u32 hitbox points
//   count x { f32 x, f32 y }            home centers
//   (count + 1) x u32 nbstart, then u16 nb entries
//   (count + 1) x u32 hbstart, then hitbox points x { f32 x, f32 y }
size_t piecegeo_size(const PieceGeo * g);
// Write g into buf (at least piecegeo_size bytes). Returns bytes written.
size_t piecegeo_write(const PieceGeo * g, uint8_t * buf);
// Build g from buf, checking everything (bad data never gives out of range indices).
// Returns 0 on success; on failure g is left empty.
int piecegeo_read(PieceGeo * g, const uint8_t * buf, size_t len);
// Piece whose hitbox contains puzzle-frame point (x, y), or -1. Hitboxes are half-open
// (a point on a shared edge belongs to one piece only).
int32_t piecegeo_at(const PieceGeo * g, float x, float y);

#endif

#ifndef __HEADER_JIGSAW_BOARD__
#define __HEADER_JIGSAW_BOARD__

#include "piecegeo.h"

#include <stdint.h>

#define BOARD_NONE 0xFFFF
#define BOARD_MAXPIECES 0xFFF0

// A single puzzle piece. Pieces are indexed as in the board's PieceGeo and never move
// between slots; only the group they belong to changes.
typedef struct {
  uint16_t group;   // Group this piece belongs to
  uint16_t next;    // Next piece in the same group (BOARD_NONE = end)
} BoardPiece;

// A set of pieces locked together. Every group uses the same frame convention:
// (x, y) is where the puzzle frame's origin (top-left of the solved puzzle) would be in
// the world, and rot (0-3, clockwise quarter turns) rotates the whole puzzle around that
// point. So a piece's world center = (x, y) + R(rot) * (its PieceGeo home center). Two
// groups are correctly aligned with each other exactly when they have the same x, y and rot.
typedef struct {
  float x, y;
  uint16_t first;   // First piece in this group's list (BOARD_NONE = dead group)
  uint16_t count;   // Pieces in this group (0 = dead group)
  uint8_t rot;
} BoardGroup;

// The puzzle table: pieces, groups, draw order. All coordinates are world units.
typedef struct {
  BoardPiece * pieces;
  BoardGroup * groups;  // One slot per piece; slots die as groups merge
  uint16_t * order;     // Draw order of live groups, bottom first
  uint32_t ngroups;     // Live groups (length of order)
  uint32_t count;       // Total pieces
  const PieceGeo * geo; // Piece geometry (not owned; must outlive the board)
  float width, height;  // Table size; table spans (0,0) to (width,height)
} Board;

// Create a solved-looking board: every piece is its own group, at its home position
// with rotation 0. Call board_scatter afterwards to make it an actual puzzle.
int board_init(Board * b, const PieceGeo * geo, float width, float height);
void board_free(Board * b);
// Throw all pieces into their own groups at random spots/rotations, spread evenly over
// the region (x, y, w, h) of the table
void board_scatter(Board * b, uint32_t seed, float x, float y, float w, float h);
// Topmost group under world point, or -1 if none. Does not change draw order.
int32_t board_pick(Board * b, float x, float y);
// Move group to the top of the draw order
void board_raise(Board * b, uint16_t group);
void board_move(Board * b, uint16_t group, float dx, float dy);
// Rotate group a quarter turn (dir +1 = clockwise, -1 = counterclockwise) around pivot
void board_rotate(Board * b, uint16_t group, int dir, float px, float py);
// Would board_drop merge anything? (Changes nothing)
int board_can_merge(Board * b, uint16_t group, float snap);
// Let go of a group: snap/merge it with any matching neighbours within snap distance.
// Repeats until nothing else merges. The dropped group keeps its id and absorbs the
// others. It moves to line up with the first one it snaps to; in the merges that follow
// (cascades) the bigger of the two stays put (ties: the dropped one), so a late cascade never
// shifts a big chunk. Returns number of merges done.
int board_drop(Board * b, uint16_t group, float snap);
int board_complete(Board * b);

// Multi-group selection. mark is one byte per group slot (b->count), nonzero = selected.
// Mark every group with a piece center inside the rectangle (corners in any order).
// Returns how many groups were newly marked.
uint32_t board_mark_rect(Board * b, float x0, float y0, float x1, float y1, uint8_t * mark);
// Move all marked groups to the top of the draw order, keeping their order among themselves
void board_raise_marked(Board * b, const uint8_t * mark);
// Bounds of the piece centers of all marked groups. Returns 0 if nothing is marked.
int board_marked_bounds(Board * b, const uint8_t * mark, float * x0, float * y0, float * x1, float * y1);
// Squish the marked groups into a tidy block centered on the middle of their bounds (kept
// on the table): rows, in their rough current order (top to bottom, then left to right),
// each piece in a slot x slot square, chunks as big as they need. Moves only; rotations,
// draw order and joins are unchanged. Returns 0 on success (1: out of memory, nothing moved).
int board_condense_marked(Board * b, const uint8_t * mark, float slot);

// Utility: world-space center of a piece, plus its rotation
void board_piece_center(Board * b, uint16_t piece, float * x, float * y);
uint8_t board_piece_rot(Board * b, uint16_t piece);
// Utility: rotate vector (x,y) by rot clockwise quarter turns
void board_rotvec(uint8_t rot, float x, float y, float * ox, float * oy);

#endif

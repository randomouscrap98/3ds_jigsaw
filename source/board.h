#ifndef __HEADER_JIGSAW_BOARD__
#define __HEADER_JIGSAW_BOARD__

#include <stdint.h>

#define BOARD_NONE 0xFFFF
#define BOARD_MAXPIECES 0xFFF0

// A single puzzle piece. Pieces are indexed row * cols + col and never move between
// slots; only the group they belong to changes.
typedef struct {
  uint16_t group;   // Group this piece belongs to
  uint16_t next;    // Next piece in the same group (BOARD_NONE = end)
} BoardPiece;

// A set of pieces locked together. Every group uses the same frame convention:
// (x, y) is where the top-left corner of grid cell (0,0) would be in the world, and
// rot (0-3, clockwise quarter turns) rotates the whole grid around that point. So a
// piece's world position = (x, y) + R(rot) * (col * cell, row * cell). Two groups are
// correctly aligned with each other exactly when they have the same x, y and rot.
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
  uint16_t cols, rows;
  float cell;           // Size of one grid cell (world units)
  float width, height;  // Table size; table spans (0,0) to (width,height)
} Board;

// Create a solved-looking board: every piece is its own group, at its home position
// with rotation 0. Call board_scatter afterwards to make it an actual puzzle.
int board_init(Board * b, uint16_t cols, uint16_t rows, float cell, float width, float height);
void board_free(Board * b);
// Throw all pieces into their own groups at random spots/rotations across the table
void board_scatter(Board * b, uint32_t seed);
// Topmost group under world point, or -1 if none. Does not change draw order.
int32_t board_pick(Board * b, float x, float y);
// Move group to the top of the draw order
void board_raise(Board * b, uint16_t group);
void board_move(Board * b, uint16_t group, float dx, float dy);
// Rotate group a quarter turn (dir +1 = clockwise, -1 = counterclockwise) around pivot
void board_rotate(Board * b, uint16_t group, int dir, float px, float py);
// Let go of a group: snap/merge it with any matching neighbours within snap distance.
// Repeats until nothing else merges. The dropped group keeps its id and absorbs the
// others (it moves to line up with them). Returns number of merges done.
int board_drop(Board * b, uint16_t group, float snap);
int board_complete(Board * b);

// Utility: world-space center of a piece, plus its rotation
void board_piece_center(Board * b, uint16_t piece, float * x, float * y);
uint8_t board_piece_rot(Board * b, uint16_t piece);
// Utility: rotate vector (x,y) by rot clockwise quarter turns
void board_rotvec(uint8_t rot, float x, float y, float * ox, float * oy);

#endif

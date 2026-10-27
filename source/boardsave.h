#ifndef __HEADER_JIGSAW_BOARDSAVE__
#define __HEADER_JIGSAW_BOARDSAVE__

#include "board.h"

#include <stddef.h>
#include <stdint.h>

#define BOARDSAVE_VERSION 2

// Board <-> flat byte buffer. Little endian, platform independent. Layout:
//   "JIGS" u16 version, u16 cols, u16 rows, f32 cell, u32 ngroups
//   version 2 only: u32 count (pieces; version 1 means cols * rows)
//   ngroups x { f32 x, f32 y, u8 rot }   (in draw order, bottom first)
//   count x u16 group index (into the list above)
// Boards with exactly cols * rows pieces (no whimsies) are written as version 1, so those
// saves stay readable by 1.0-1.1.
typedef struct {
  uint16_t version;
  uint16_t cols, rows;
  float cell;
  uint32_t ngroups;
  uint32_t count;     // Pieces
} BoardSaveInfo;

// Bytes needed to save this board
size_t boardsave_size(Board * b);
// Write board into buf (must be at least boardsave_size bytes). Returns bytes written.
size_t boardsave_write(Board * b, uint8_t * buf);
// Read only the header. Returns 0 on success.
int boardsave_peek(const uint8_t * buf, size_t len, BoardSaveInfo * info);
// Restore a board from buf. Board must already be initialized with the same cols/rows.
// If the saved cell size differs from the board's, positions are scaled to match.
// On failure the board is left untouched. Returns 0 on success.
int boardsave_read(Board * b, const uint8_t * buf, size_t len);

#endif

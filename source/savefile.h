#ifndef __HEADER_JIGSAW_SAVEFILE__
#define __HEADER_JIGSAW_SAVEFILE__

#include "board.h"

#include <stddef.h>
#include <stdint.h>

#define SAVEFILE_MAGIC   "3DSJGSAW"
#define SAVEFILE_VERSION 1
#define SAVEFILE_HEADER  64      // Header size written by this version
#define SAVEFILE_MINHEADER 23    // Smallest header we can read (fields below; later ones
                                 // default to 0 when the header is shorter)
#define SAVEFILE_MUSIC_ORDER   0 // Zero so saves from before the field play in order
#define SAVEFILE_MUSIC_OFF     1
#define SAVEFILE_MUSIC_SHUFFLE 2

// A whole save file: this header, then a boardsave blob. Little endian. Header layout:
//   0  char[8] "3DSJGSAW"
//   8  u16 save version          10 u16 header size (boardsave starts here)
//   12 u8[3] app version that wrote it (major, minor, micro)
//   15 u8 cut style              16 u16 target piece count
//   18 u8 table color            19 u32 play time, seconds
//   23 u8 music mode (SAVEFILE_MUSIC_*; 1.1, was reserved = 0 = in order)
//   24 u8 whimsy level (WHIMSY_*; 1.2, 0 = none)
//   25 u8 whimsy shape set (WHIMSY_SET_*; 1.2, 0 = cute)
//   26.. reserved, zero
typedef struct {
  uint16_t version;
  uint16_t size;
  uint8_t appver[3];
  uint8_t style;
  uint16_t target;
  uint8_t table;
  uint32_t seconds;   // Play time
  uint8_t music;      // SAVEFILE_MUSIC_*
  uint8_t whimsy;     // WHIMSY_* level
  uint8_t whimsyset;  // WHIMSY_SET_*
} SaveHeader;

// What a save says about a puzzle in progress, without loading the board
typedef struct {
  SaveHeader header;
  uint32_t pieces;    // Pieces (including whimsies)
  uint32_t groups;    // Groups left; 1 = complete
} SaveInfo;

// Return codes for the readers
#define SAVEFILE_OK 0
#define SAVEFILE_BAD 1     // Not a save we understand (or cut short)
#define SAVEFILE_NEWER 2   // Written by a newer save version

// Fill in the current version + header size and zero everything else
void savefile_init(SaveHeader * h);
// Bytes needed to save this board
size_t savefile_size(Board * b);
// Write header + board into buf (at least savefile_size bytes)
void savefile_write(const SaveHeader * h, Board * b, uint8_t * buf);
// Parse just the header. Returns SAVEFILE_*.
int savefile_readheader(const uint8_t * buf, size_t len, SaveHeader * h);
// Header + board summary. buf can be just the start of the file (a couple hundred bytes
// is plenty). Returns SAVEFILE_*.
int savefile_peek(const uint8_t * buf, size_t len, SaveInfo * info);
// Restore the board from a save whose header was already read with savefile_readheader.
// Board must already be initialized with the save's grid. Returns SAVEFILE_*.
int savefile_readboard(const uint8_t * buf, size_t len, const SaveHeader * h, Board * b);

#endif

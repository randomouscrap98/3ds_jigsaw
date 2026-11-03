#ifndef __HEADER_JIGSAW_SAVEFILE__
#define __HEADER_JIGSAW_SAVEFILE__

#include "board.h"

#include <stddef.h>
#include <stdint.h>

#define SAVEFILE_MAGIC   "3DSJGSAW"
#define SAVEFILE_VERSION 1
#define SAVEFILE_HEADER  64      // Header size written by this version
#define SAVEFILE_MUSIC_ORDER   0 // Zero so saves from before the field play in order
#define SAVEFILE_MUSIC_OFF     1
#define SAVEFILE_MUSIC_SHUFFLE 2
#define SAVEFILE_MUSIC_MODES   3
#define SAVEFILE_VOLUME_HIGH   0 // Zero so saves from before the field stay at full volume
#define SAVEFILE_VOLUME_LOW    1
#define SAVEFILE_VOLUME_MEDIUM 2
#define SAVEFILE_VOLUMES       3
#define SAVEFILE_TOP_PICTURE   0 // Top screen: picture, hold Y for the info panel (zero: the old way)
#define SAVEFILE_TOP_INFO      1 // Top screen: info panel, hold Y for the picture
#define SAVEFILE_TOPS          2

// A whole save file: this header, then a boardsave blob. Little endian. Header layout
// (byte offsets). Fields after SAVEFILE_AT_MUSIC were added later: one past the end of an
// older (shorter) header reads as 0, so 0 must always mean the old behavior. A new field goes
// at SAVEFILE_AT_END and moves it on; never move a field (test_savefile_layout pins them).
#define SAVEFILE_AT_MAGIC     0   // char[8] "3DSJGSAW"
#define SAVEFILE_AT_VERSION   8   // u16 save version
#define SAVEFILE_AT_SIZE      10  // u16 header size (boardsave starts here)
#define SAVEFILE_AT_APPVER    12  // u8[3] app version that wrote it (major, minor, micro)
#define SAVEFILE_AT_STYLE     15  // u8 cut style
#define SAVEFILE_AT_TARGET    16  // u16 target piece count
#define SAVEFILE_AT_TABLE     18  // u8 table color
#define SAVEFILE_AT_SECONDS   19  // u32 play time, seconds
#define SAVEFILE_AT_MUSIC     23  // u8 music mode (SAVEFILE_MUSIC_*; 1.1, was reserved = 0 = in order)
#define SAVEFILE_AT_WHIMSY    24  // u8 whimsy level (WHIMSY_*; 1.2, 0 = none)
#define SAVEFILE_AT_WHIMSYSET 25  // u8 whimsy shape set (WHIMSY_SET_*; 1.2, 0 = cute)
#define SAVEFILE_AT_CUTHASH   26  // u32 cutter_hash of the cut the board was saved with (1.2, 0 = unknown)
#define SAVEFILE_AT_SEED      30  // u32 cut seed (1.2, 0 = unknown: savefile_cutseed of the picture's
                                  // name). Kept so a renamed picture still cuts the same way.
#define SAVEFILE_AT_OVERLAY   34  // u8 overlay (OVERLAY_*; 1.2, 0 = none)
#define SAVEFILE_AT_VOLUME    35  // u8 music volume (SAVEFILE_VOLUME_*; 1.3, 0 = high)
#define SAVEFILE_AT_TOP       36  // u8 top screen mode (SAVEFILE_TOP_*; 1.4, 0 = picture)
#define SAVEFILE_AT_END       37  // First unused byte; the rest up to SAVEFILE_HEADER is zero
#define SAVEFILE_MINHEADER SAVEFILE_AT_MUSIC  // Smallest header we can read (1.0's)
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
  uint32_t cuthash;   // The cut's fingerprint (0 = unknown, from before 1.2)
  uint32_t seed;      // The cut's seed (0 = unknown: savefile_cutseed of the name)
  uint8_t overlay;    // OVERLAY_*
  uint8_t volume;     // SAVEFILE_VOLUME_*
  uint8_t top;        // SAVEFILE_TOP_*
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

// Cut seed for a new puzzle of picture name (file name with extension)
uint32_t savefile_cutseed(const char * name, uint32_t target, uint8_t style);
// Seed a puzzle loaded from this header uses: its own, else the one from name
uint32_t savefile_seedfor(const SaveHeader * h, const char * name);
// Copy of a save (len bytes, header already read into h) with the seed field set; the
// header grows to SAVEFILE_HEADER if it's too short to hold it. malloc'd, NULL if out of memory.
uint8_t * savefile_withseed(const uint8_t * buf, size_t len, const SaveHeader * h, uint32_t seed, size_t * outlen);
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

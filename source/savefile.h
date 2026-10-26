#ifndef __HEADER_JIGSAW_SAVEFILE__
#define __HEADER_JIGSAW_SAVEFILE__

#include <stddef.h>
#include <stdint.h>

#define SAVEFILE_MAGIC   "3DSJGSAW"
#define SAVEFILE_VERSION 1
#define SAVEFILE_HEADER  64      // Header size written by this version
#define SAVEFILE_MINHEADER 19    // Smallest header we can read (fields below)

// Save file header, followed by a boardsave blob. Little endian. Layout:
//   0  char[8] "3DSJGSAW"
//   8  u16 save version          10 u16 header size (boardsave starts here)
//   12 u8[3] app version that wrote it (major, minor, micro)
//   15 u8 cut style              16 u16 target piece count
//   18 u8 table color            19.. reserved, zero
typedef struct {
  uint16_t version;
  uint16_t size;
  uint8_t appver[3];
  uint8_t style;
  uint16_t target;
  uint8_t table;
} SaveHeader;

// Fill in the current version + header size and zero everything else
void savefile_init(SaveHeader * h);
// Write SAVEFILE_HEADER bytes into buf
void savefile_write(const SaveHeader * h, uint8_t * buf);
// Parse the header. Returns 0 on success, 1 if it isn't a save (or is cut short),
// 2 if it's from a newer save version.
int savefile_read(const uint8_t * buf, size_t len, SaveHeader * h);

#endif

#ifndef __HEADER_JIGSAW_STORAGE__
#define __HEADER_JIGSAW_STORAGE__

#include <stddef.h>
#include <stdint.h>

#define STORAGE_ROOT    "sdmc:/3ds/jigsaw"
#define STORAGE_PUZZLES STORAGE_ROOT "/puzzles"
#define STORAGE_CACHE   STORAGE_ROOT "/cache"
#define STORAGE_SAVES   STORAGE_ROOT "/saves"
#define STORAGE_MAXPATH 512

// What a save file says about a puzzle in progress
typedef struct {
  uint32_t target;    // Piece count option it was started with
  uint8_t style;      // Cut style (PGFX_STYLE_*)
  uint32_t pieces;    // Actual pieces (cols * rows)
  uint32_t groups;    // Groups left; 1 = complete
} StorageSaveInfo;

// Make sure all the folders exist
void storage_init(void);
void storage_puzzlepath(const char * name, char * out);
void storage_cachepath(const char * name, uint32_t target, uint8_t style, char * out);
void storage_savepath(const char * name, char * out);
// Whole file into a malloc'd buffer. Returns 0 on success.
int storage_readfile(const char * path, uint8_t ** buf, size_t * len);
// Write via temp file + rename so a crash can't leave half a file. Returns 0 on success.
int storage_writefile(const char * path, const uint8_t * buf, size_t len);
// Save files start with a u32: target piece count in the low 24 bits, cut style in the
// high 8 (older saves have 0 there = classic). Then a boardsave blob.
#define STORAGE_SAVE_TARGET(v) ((v) & 0xFFFFFF)
#define STORAGE_SAVE_STYLE(v) ((v) >> 24)
#define STORAGE_SAVE_PREFIX(target, style) (((uint32_t)(style) << 24) | ((target) & 0xFFFFFF))
int storage_peeksave(const char * name, StorageSaveInfo * info);

#endif

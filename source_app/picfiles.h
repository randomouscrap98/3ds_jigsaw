#ifndef __HEADER_JIGSAW_PICFILES__
#define __HEADER_JIGSAW_PICFILES__

#include <stddef.h>
#include <stdint.h>

// A picture's files as one: the picture, its save and its caches. Rename, delete, total
// size. No 3DS deps (host-tested); the folders are passed in.
// Renaming: the cut seed comes from the picture's name, so a save without its own seed
// gets the old name's written in first: the puzzle keeps its pieces (and the caches stay
// valid, they check the seed, not the name).

#define PICFILES_OK 0
#define PICFILES_TAKEN 1    // A picture or save with the new name is already there
#define PICFILES_FAILED 2   // A file couldn't be written, renamed or deleted (nothing was lost)

typedef struct {
  const char * puzzles;
  const char * saves;
  const char * cache;
} PicDirs;

// Rename picture name (a file in puzzles, with extension) to newbase + the same extension.
// newbase must already be clean (storage_cleanbase). newname gets the new file name.
// Renaming to the same name is OK and does nothing.
int picfiles_rename(const PicDirs * d, const char * name, const char * newbase, char * newname, size_t newlen);
// Delete the picture, then its save and caches (only if the picture went)
int picfiles_delete(const PicDirs * d, const char * name);
// Bytes used by the picture, its save and its caches (missing files count 0)
uint64_t picfiles_size(const PicDirs * d, const char * name);

#endif

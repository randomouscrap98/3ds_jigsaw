#ifndef __HEADER_JIGSAW_PICRENAME__
#define __HEADER_JIGSAW_PICRENAME__

#include <stddef.h>

// Renaming a picture along with its save and caches. The cut seed comes from the picture's
// name, so a save without its own seed gets the old name's written in first: the puzzle
// keeps its pieces (and the caches stay valid, they check the seed, not the name). No 3DS
// deps (host-tested); the folders are passed in.

#define PICRENAME_OK 0
#define PICRENAME_TAKEN 1    // A picture or save with the new name is already there
#define PICRENAME_FAILED 2   // A file couldn't be written or renamed (nothing was lost)

typedef struct {
  const char * puzzles;
  const char * saves;
  const char * cache;
} PicDirs;

// Rename picture name (a file in puzzles, with extension) to newbase + the same extension.
// newbase must already be clean (storage_cleanbase). newname gets the new file name.
// Renaming to the same name is OK and does nothing.
int picrename(const PicDirs * d, const char * name, const char * newbase, char * newname, size_t newlen);

#endif

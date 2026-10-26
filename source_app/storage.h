#ifndef __HEADER_JIGSAW_STORAGE__
#define __HEADER_JIGSAW_STORAGE__

#include <stddef.h>
#include <stdint.h>

#define STORAGE_ROOT    "sdmc:/3ds/jigsaw"
#define STORAGE_PUZZLES STORAGE_ROOT "/puzzles"
#define STORAGE_CACHE   STORAGE_ROOT "/cache"
#define STORAGE_SAVES   STORAGE_ROOT "/saves"
#define STORAGE_MAXPATH 512

// Make sure all the folders exist
void storage_init(void);
void storage_puzzlepath(const char * name, char * out);
void storage_cachepath(const char * name, uint32_t target, uint8_t style, char * out);
void storage_savepath(const char * name, char * out);
// Whole file into a malloc'd buffer. Returns 0 on success.
int storage_readfile(const char * path, uint8_t ** buf, size_t * len);
// Up to max bytes from the start of a file. Returns 0 if anything was read.
int storage_readhead(const char * path, uint8_t * buf, size_t max, size_t * len);
// Write via temp file + rename so a crash can't leave half a file. Returns 0 on success.
int storage_writefile(const char * path, const uint8_t * buf, size_t len);
// For writers that stream: write to storage_tmppath(path), then storage_commit moves it
// into place (or removes it if ok is 0). Returns 0 on success.
#define STORAGE_MAXTMP (STORAGE_MAXPATH + 8)
void storage_tmppath(const char * path, char * out);
int storage_commit(const char * path, int ok);

#endif

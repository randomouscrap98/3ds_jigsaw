#ifndef __HEADER_JIGSAW_STORAGE__
#define __HEADER_JIGSAW_STORAGE__

#include <stddef.h>
#include <stdint.h>

#define STORAGE_ROOT    "sdmc:/3ds/jigsaw"
#define STORAGE_PUZZLES STORAGE_ROOT "/puzzles"
#define STORAGE_CACHE   STORAGE_ROOT "/cache"
#define STORAGE_SAVES   STORAGE_ROOT "/saves"
#define STORAGE_MUSIC   STORAGE_ROOT "/music"
#define STORAGE_MAXPATH 512

// Make sure all the folders exist
void storage_init(void);
void storage_puzzlepath(const char * name, char * out);
// Cache file for a picture cut a given way:
// "<name>_<target>[_s<style>][_w<whimsy>[_g<set>]][_o<overlay>].bin"
// (each part only when nonzero, so 1.0-1.1 names are unchanged)
void storage_cachepath(const char * name, uint32_t target, uint8_t style, uint8_t whimsy, uint8_t set, uint8_t overlay,
    char * out);
void storage_savepath(const char * name, char * out);
// 1 if file (a name in the cache folder) is one of image name's caches, any count/style/
// whimsies/overlay: "<name>_<digits>[_s<digits>][_w<digits>[_g<digits>]][_o<digits>].bin"
int storage_iscache(const char * name, const char * file);
// 1 if file is a leftover temp file of one of image name's caches ("<cache>.bin.tmp": a
// cache write cut short by a crash or power off)
int storage_iscachetmp(const char * name, const char * file);
// An uploaded file's name made safe for a picture: no folder parts or extension, only
// letters, digits, spaces and - _ ( ) ' ,, at most STORAGE_MAXBASE characters, never empty
// ("picture" if nothing usable is left)
#define STORAGE_MAXBASE 60
void storage_cleanname(const char * raw, char * out, size_t outlen);
// Same rules for a typed base name: nothing is cut off (a dot becomes _)
void storage_cleanbase(const char * raw, char * out, size_t outlen);
// A file name in dir not yet taken: "<base><ext>", else "<base> (2)<ext>", "<base> (3)<ext>"...
// Returns 0 on success, 1 if none is free (after 999).
int storage_freename(const char * dir, const char * base, const char * ext, char * name, size_t namelen);
// Delete every cache of image name except the file at keep (a full cache path), and any
// leftover cache temp files of it
void storage_cleancaches(const char * name, const char * keep);
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

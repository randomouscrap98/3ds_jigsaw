#include "picfiles.h"
#include "savefile.h"
#include "storage.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int picfiles_exists(const char * path) {
  struct stat st;
  return stat(path, &st) == 0;
}

// Make sure the save at path carries its cut seed (as computed from name). 0 = fine (or
// nothing to do: no save, or one we can't read, which the game refuses either way).
static int picfiles_keepseed(const char * path, const char * name) {
  uint8_t * buf;
  size_t len;
  if(storage_readfile(path, &buf, &len)) return 0;
  SaveHeader h;
  int r = 0;
  if(savefile_readheader(buf, len, &h) == SAVEFILE_OK && !h.seed) {
    size_t outlen;
    uint8_t * out = savefile_withseed(buf, len, &h, savefile_cutseed(name, h.target, h.style), &outlen);
    r = !out || storage_writefile(path, out, outlen);
    free(out);
  }
  free(buf);
  return r;
}

// Names of picture name's caches and leftover cache temp files in the cache folder
// (malloc'd; count in *n). Collected first so changes don't upset the directory walk.
static char (*picfiles_cachelist(const PicDirs * d, const char * name, int * n))[STORAGE_MAXPATH] {
  *n = 0;
  DIR * dir = opendir(d->cache);
  if(!dir) return NULL;
  char (*files)[STORAGE_MAXPATH] = NULL;
  struct dirent * de;
  while((de = readdir(dir))) {
    if(!storage_iscache(name, de->d_name) && !storage_iscachetmp(name, de->d_name)) continue;
    void * more = realloc(files, sizeof(*files) * (*n + 1));
    if(!more) break;
    files = more;
    snprintf(files[(*n)++], STORAGE_MAXPATH, "%s", de->d_name);
  }
  closedir(dir);
  return files;
}

// Caches follow the picture (a cache name is "<picture name>_<...>.bin"); one that can't be
// renamed is removed (it's rebuilt when needed), as are leftover cache temp files. newname
// NULL: remove them all.
static void picfiles_caches(const PicDirs * d, const char * name, const char * newname) {
  int count;
  char (*files)[STORAGE_MAXPATH] = picfiles_cachelist(d, name, &count);
  for(int i = 0; i < count; i++) {
    char from[STORAGE_MAXPATH], to[STORAGE_MAXPATH];
    snprintf(from, sizeof(from), "%s/%s", d->cache, files[i]);
    if(!newname || storage_iscachetmp(name, files[i])) {
      remove(from);
      continue;
    }
    snprintf(to, sizeof(to), "%s/%s%s", d->cache, newname, files[i] + strlen(name));
    if(rename(from, to)) remove(from);
  }
  free(files);
}

int picfiles_rename(const PicDirs * d, const char * name, const char * newbase, char * newname, size_t newlen) {
  const char * ext = strrchr(name, '.');
  snprintf(newname, newlen, "%s%s", newbase, ext ? ext : "");
  if(!strcmp(newname, name)) return PICFILES_OK;
  char pic[STORAGE_MAXPATH], newpic[STORAGE_MAXPATH], sav[STORAGE_MAXPATH], newsav[STORAGE_MAXPATH];
  snprintf(pic, sizeof(pic), "%s/%s", d->puzzles, name);
  snprintf(newpic, sizeof(newpic), "%s/%s", d->puzzles, newname);
  snprintf(sav, sizeof(sav), "%s/%s.sav", d->saves, name);
  snprintf(newsav, sizeof(newsav), "%s/%s.sav", d->saves, newname);
  // Only a change of case may "find" itself (the SD card ignores case)
  if(strcasecmp(newname, name) && (picfiles_exists(newpic) || picfiles_exists(newsav))) return PICFILES_TAKEN;
  int hassave = picfiles_exists(sav);
  if(hassave && (picfiles_keepseed(sav, name) || rename(sav, newsav))) return PICFILES_FAILED;
  if(rename(pic, newpic)) {
    if(hassave) rename(newsav, sav);
    return PICFILES_FAILED;
  }
  picfiles_caches(d, name, newname);
  return PICFILES_OK;
}

int picfiles_delete(const PicDirs * d, const char * name) {
  char path[STORAGE_MAXPATH];
  snprintf(path, sizeof(path), "%s/%s", d->puzzles, name);
  if(remove(path)) return PICFILES_FAILED;
  snprintf(path, sizeof(path), "%s/%s.sav", d->saves, name);
  remove(path);
  picfiles_caches(d, name, NULL);
  return PICFILES_OK;
}

static uint64_t picfiles_filesize(const char * path) {
  struct stat st;
  return stat(path, &st) == 0 ? (uint64_t)st.st_size : 0;
}

uint64_t picfiles_size(const PicDirs * d, const char * name) {
  char path[STORAGE_MAXPATH];
  snprintf(path, sizeof(path), "%s/%s", d->puzzles, name);
  uint64_t total = picfiles_filesize(path);
  snprintf(path, sizeof(path), "%s/%s.sav", d->saves, name);
  total += picfiles_filesize(path);
  int count;
  char (*files)[STORAGE_MAXPATH] = picfiles_cachelist(d, name, &count);
  for(int i = 0; i < count; i++) {
    snprintf(path, sizeof(path), "%s/%s", d->cache, files[i]);
    total += picfiles_filesize(path);
  }
  free(files);
  return total;
}

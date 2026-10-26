#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void storage_init(void) {
  mkdir("sdmc:/3ds", 0777);
  mkdir(STORAGE_ROOT, 0777);
  mkdir(STORAGE_PUZZLES, 0777);
  mkdir(STORAGE_CACHE, 0777);
  mkdir(STORAGE_SAVES, 0777);
}

void storage_puzzlepath(const char * name, char * out) {
  snprintf(out, STORAGE_MAXPATH, "%s/%s", STORAGE_PUZZLES, name);
}

void storage_cachepath(const char * name, uint32_t target, uint8_t style, char * out) {
  // Classic keeps the original name
  if(style) snprintf(out, STORAGE_MAXPATH, "%s/%s_%lu_s%u.bin", STORAGE_CACHE, name, (unsigned long)target, style);
  else snprintf(out, STORAGE_MAXPATH, "%s/%s_%lu.bin", STORAGE_CACHE, name, (unsigned long)target);
}

void storage_savepath(const char * name, char * out) {
  snprintf(out, STORAGE_MAXPATH, "%s/%s.sav", STORAGE_SAVES, name);
}

int storage_readfile(const char * path, uint8_t ** buf, size_t * len) {
  *buf = NULL;
  *len = 0;
  FILE * f = fopen(path, "rb");
  if(!f) return 1;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if(size <= 0) {
    fclose(f);
    return 2;
  }
  *buf = malloc(size);
  if(!*buf) {
    fclose(f);
    return 3;
  }
  if(fread(*buf, 1, size, f) != (size_t)size) {
    free(*buf);
    *buf = NULL;
    fclose(f);
    return 4;
  }
  fclose(f);
  *len = size;
  return 0;
}

int storage_readhead(const char * path, uint8_t * buf, size_t max, size_t * len) {
  FILE * f = fopen(path, "rb");
  if(!f) return 1;
  *len = fread(buf, 1, max, f);
  fclose(f);
  return *len ? 0 : 2;
}

void storage_tmppath(const char * path, char * out) {
  snprintf(out, STORAGE_MAXTMP, "%s.tmp", path);
}

int storage_commit(const char * path, int ok) {
  char tmp[STORAGE_MAXTMP];
  storage_tmppath(path, tmp);
  if(!ok) {
    remove(tmp);
    return 1;
  }
  // The 3DS filesystem won't rename over an existing file
  remove(path);
  if(rename(tmp, path) != 0) {
    remove(tmp);
    return 2;
  }
  return 0;
}

int storage_writefile(const char * path, const uint8_t * buf, size_t len) {
  char tmp[STORAGE_MAXTMP];
  storage_tmppath(path, tmp);
  FILE * f = fopen(tmp, "wb");
  if(!f) return 1;
  size_t written = fwrite(buf, 1, len, f);
  int ok = written == len;
  if(fclose(f) != 0) ok = 0;
  return storage_commit(path, ok) ? 2 : 0;
}

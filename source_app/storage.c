#include "storage.h"
#include "boardsave.h"

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

void storage_cachepath(const char * name, uint32_t target, char * out) {
  snprintf(out, STORAGE_MAXPATH, "%s/%s_%lu.bin", STORAGE_CACHE, name, (unsigned long)target);
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

int storage_writefile(const char * path, const uint8_t * buf, size_t len) {
  char tmp[STORAGE_MAXPATH + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  FILE * f = fopen(tmp, "wb");
  if(!f) return 1;
  size_t written = fwrite(buf, 1, len, f);
  if(fclose(f) != 0 || written != len) {
    remove(tmp);
    return 2;
  }
  // The 3DS filesystem won't rename over an existing file
  remove(path);
  if(rename(tmp, path) != 0) return 3;
  return 0;
}

int storage_peeksave(const char * name, StorageSaveInfo * info) {
  char path[STORAGE_MAXPATH];
  storage_savepath(name, path);
  FILE * f = fopen(path, "rb");
  if(!f) return 1;
  uint8_t head[64];
  size_t len = fread(head, 1, sizeof(head), f);
  fclose(f);
  if(len < 4) return 2;
  BoardSaveInfo bi;
  if(boardsave_peek(head + 4, len - 4, &bi)) return 3;
  info->target = head[0] | (head[1] << 8) | (head[2] << 16) | ((uint32_t)head[3] << 24);
  info->pieces = (uint32_t)bi.cols * bi.rows;
  info->groups = bi.ngroups;
  return 0;
}

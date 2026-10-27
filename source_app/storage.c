#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

void storage_init(void) {
  mkdir("sdmc:/3ds", 0777);
  mkdir(STORAGE_ROOT, 0777);
  mkdir(STORAGE_PUZZLES, 0777);
  mkdir(STORAGE_CACHE, 0777);
  mkdir(STORAGE_SAVES, 0777);
  mkdir(STORAGE_MUSIC, 0777);
}

void storage_puzzlepath(const char * name, char * out) {
  snprintf(out, STORAGE_MAXPATH, "%s/%s", STORAGE_PUZZLES, name);
}

void storage_cachepath(const char * name, uint32_t target, uint8_t style, uint8_t whimsy, uint8_t set, char * out) {
  // Classic without whimsies keeps the original name
  char st[8] = "", wh[16] = "";
  if(style) snprintf(st, sizeof(st), "_s%u", style);
  if(whimsy && set) snprintf(wh, sizeof(wh), "_w%u_g%u", whimsy, set);
  else if(whimsy) snprintf(wh, sizeof(wh), "_w%u", whimsy);
  snprintf(out, STORAGE_MAXPATH, "%s/%s_%lu%s%s.bin", STORAGE_CACHE, name, (unsigned long)target, st, wh);
}

void storage_savepath(const char * name, char * out) {
  snprintf(out, STORAGE_MAXPATH, "%s/%s.sav", STORAGE_SAVES, name);
}

static const char * storage_digits(const char * s) {
  if(*s < '0' || *s > '9') return NULL;
  while(*s >= '0' && *s <= '9') s++;
  return s;
}

int storage_iscache(const char * name, const char * file) {
  size_t n = strlen(name);
  if(strncmp(file, name, n) || file[n] != '_') return 0;
  const char * p = storage_digits(file + n + 1);
  if(p && p[0] == '_' && p[1] == 's') p = storage_digits(p + 2);
  if(p && p[0] == '_' && p[1] == 'w') {
    p = storage_digits(p + 2);
    if(p && p[0] == '_' && p[1] == 'g') p = storage_digits(p + 2);
  }
  return p && !strcmp(p, ".bin");
}

// Safe-character copy of [start, end) (see storage_cleanname)
static void storage_cleanspan(const char * start, const char * end, char * out, size_t outlen) {
  size_t n = 0, max = outlen - 1 < STORAGE_MAXBASE ? outlen - 1 : STORAGE_MAXBASE;
  for(const char * p = start; p < end && n < max; p++) {
    char c = *p;
    int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(" -_()',", c);
    if(c == 0) break;
    out[n++] = ok ? c : '_';
  }
  // No leading or trailing spaces/dots
  while(n > 0 && out[n - 1] == ' ') n--;
  size_t lead = 0;
  while(lead < n && out[lead] == ' ') lead++;
  memmove(out, out + lead, n - lead);
  n -= lead;
  out[n] = 0;
  if(!n) snprintf(out, outlen, "picture");
}

void storage_cleanname(const char * raw, char * out, size_t outlen) {
  // Last path part only (browsers send just the name, but don't trust it)
  const char * start = raw;
  for(const char * p = raw; *p; p++) if(*p == '/' || *p == '\\') start = p + 1;
  // Drop the extension
  const char * end = start + strlen(start);
  for(const char * p = end; p > start; p--) {
    if(p[-1] == '.') {
      end = p - 1;
      break;
    }
  }
  storage_cleanspan(start, end, out, outlen);
}

void storage_cleanbase(const char * raw, char * out, size_t outlen) {
  storage_cleanspan(raw, raw + strlen(raw), out, outlen);
}

int storage_freename(const char * dir, const char * base, const char * ext, char * name, size_t namelen) {
  for(int i = 1; i <= 999; i++) {
    if(i == 1) snprintf(name, namelen, "%s%s", base, ext);
    else snprintf(name, namelen, "%s (%d)%s", base, i, ext);
    char path[STORAGE_MAXPATH];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    struct stat st;
    if(stat(path, &st) != 0) return 0;
  }
  return 1;
}

int storage_iscachetmp(const char * name, const char * file) {
  size_t n = strlen(file);
  if(n < 4 || strcmp(file + n - 4, ".tmp") || n - 4 >= STORAGE_MAXPATH) return 0;
  char base[STORAGE_MAXPATH];
  memcpy(base, file, n - 4);
  base[n - 4] = 0;
  return storage_iscache(name, base);
}

void storage_cleancaches(const char * name, const char * keep) {
  DIR * d = opendir(STORAGE_CACHE);
  if(!d) return;
  // Collect first, then delete, so removing doesn't upset the directory walk
  char (*doomed)[STORAGE_MAXPATH] = NULL;
  int count = 0;
  struct dirent * de;
  while((de = readdir(d))) {
    int tmp = storage_iscachetmp(name, de->d_name);
    if(!tmp && !storage_iscache(name, de->d_name)) continue;
    char path[STORAGE_MAXPATH];
    snprintf(path, sizeof(path), "%s/%s", STORAGE_CACHE, de->d_name);
    if(!tmp && !strcmp(path, keep)) continue;
    void * more = realloc(doomed, sizeof(*doomed) * (count + 1));
    if(!more) break;
    doomed = more;
    memcpy(doomed[count++], path, STORAGE_MAXPATH);
  }
  closedir(d);
  for(int i = 0; i < count; i++) remove(doomed[i]);
  free(doomed);
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

#include "defaults.h"
#include "storage.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
  progress_fn cb;
  void * udata;
  long done, total, lastdraw;   // Bytes
  char * buf;
} DefaultsCopy;

static void defaults_report(DefaultsCopy * dc, int force) {
  if(!dc->cb || (!force && dc->done - dc->lastdraw < DEFAULTS_REDRAW)) return;
  dc->lastdraw = dc->done;
  // Progress in KB so it fits an int comfortably
  dc->cb(dc->udata, "Installing default puzzles", dc->done / 1024, dc->total / 1024);
}

static int defaults_copyfile(DefaultsCopy * dc, const char * src, const char * dst) {
  FILE * in = fopen(src, "rb");
  if(!in) return 1;
  char tmp[STORAGE_MAXPATH + 8];
  snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
  FILE * out = fopen(tmp, "wb");
  if(!out) {
    fclose(in);
    return 2;
  }
  int ok = 1;
  size_t n;
  while(ok && (n = fread(dc->buf, 1, DEFAULTS_CHUNK, in)) > 0) {
    ok = fwrite(dc->buf, 1, n, out) == n;
    dc->done += n;
    defaults_report(dc, 0);
  }
  fclose(in);
  if(fclose(out) != 0) ok = 0;
  if(!ok || rename(tmp, dst) != 0) {
    remove(tmp);
    return 3;
  }
  return 0;
}

int defaults_install(progress_fn cb, void * udata) {
  struct stat st;
  if(stat(DEFAULTS_MARKER, &st) == 0) return 0;
  DefaultsCopy dc = { .cb = cb, .udata = udata };
  char src[STORAGE_MAXPATH], dst[STORAGE_MAXPATH];
  // First pass: total size of what needs copying, for the progress bar
  DIR * d = opendir(DEFAULTS_SRC);
  if(!d) return 1;
  struct dirent * de;
  while((de = readdir(d))) {
    if(de->d_name[0] == '.') continue;
    snprintf(src, sizeof(src), "%s/%s", DEFAULTS_SRC, de->d_name);
    snprintf(dst, sizeof(dst), "%s/%s", STORAGE_PUZZLES, de->d_name);
    if(stat(dst, &st) != 0 && stat(src, &st) == 0) dc.total += st.st_size;
  }
  closedir(d);
  dc.buf = malloc(DEFAULTS_CHUNK);
  if(!dc.buf) return 2;
  defaults_report(&dc, 1);
  int err = 0;
  d = opendir(DEFAULTS_SRC);
  if(!d) err = 1;
  while(d && !err && (de = readdir(d))) {
    if(de->d_name[0] == '.') continue;
    snprintf(src, sizeof(src), "%s/%s", DEFAULTS_SRC, de->d_name);
    snprintf(dst, sizeof(dst), "%s/%s", STORAGE_PUZZLES, de->d_name);
    if(stat(dst, &st) == 0) continue;
    err = defaults_copyfile(&dc, src, dst);
  }
  if(d) closedir(d);
  free(dc.buf);
  // Only mark done if everything made it; otherwise try again next launch
  if(!err) {
    FILE * f = fopen(DEFAULTS_MARKER, "wb");
    if(f) fclose(f);
  }
  return err;
}

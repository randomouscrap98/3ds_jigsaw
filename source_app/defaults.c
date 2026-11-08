#include "defaults.h"
#include "boardsave.h"
#include "imageload.h"
#include "layout.h"
#include "savefile.h"
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
  const char * msg;
} DefaultsCopy;

static void defaults_report(DefaultsCopy * dc, int force) {
  if(!dc->cb || (!force && dc->done - dc->lastdraw < DEFAULTS_REDRAW)) return;
  dc->lastdraw = dc->done;
  // Progress in KB so it fits an int comfortably
  dc->cb(dc->udata, dc->msg, dc->done / 1024, dc->total / 1024);
}

static int defaults_copyfile(DefaultsCopy * dc, const char * src, const char * dst) {
  FILE * in = fopen(src, "rb");
  if(!in) return 1;
  char tmp[STORAGE_MAXTMP];
  storage_tmppath(dst, tmp);
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
  return storage_commit(dst, ok) ? 3 : 0;
}

// Version the marker says was installed: 0 = none yet, an empty (pre-1.5) marker = 1
static int defaults_installed(void) {
  FILE * f = fopen(DEFAULTS_MARKER, "rb");
  if(!f) return 0;
  int v = 1;
  if(fscanf(f, "%d", &v) != 1 || v < 1) v = 1;
  fclose(f);
  return v;
}

// Would the save of picture name fit a picture of w x h? Its cut comes from the grid its piece count
// gives for the picture's shape, and the save records that grid. No save = yes; unreadable = no.
static int defaults_savefits(const char * name, int w, int h) {
  char path[STORAGE_MAXPATH];
  storage_savepath(name, path);
  uint8_t buf[DEFAULTS_SAVEHEAD];
  size_t len = 0;
  SaveHeader hdr;
  BoardSaveInfo info;
  Layout l;
  if(storage_readhead(path, buf, sizeof(buf), &len)) return 1;
  if(savefile_readheader(buf, len, &hdr) || hdr.size >= len) return 0;
  if(boardsave_peek(buf + hdr.size, len - hdr.size, &info) || layout_crop(&l, w, h, hdr.target)) return 0;
  return l.cols == info.cols && l.rows == info.rows;
}

// Should bundled picture src go to dst? Missing: only when nothing's been installed yet (deleted
// defaults stay deleted). There: when the bundled one is bigger. Either way not if a save wouldn't
// fit it. Non-pictures (credits) are only ever copied when missing.
static int defaults_wanted(const char * name, const char * src, const char * dst, int fresh) {
  struct stat st;
  int there = stat(dst, &st) == 0, w0, h0, w1, h1;
  int pic = !imageload_size(src, &w1, &h1);
  if(!there) return fresh && (!pic || defaults_savefits(name, w1, h1));
  if(!pic || imageload_size(dst, &w0, &h0) || (long)w1 * h1 <= (long)w0 * h0) return 0;
  return defaults_savefits(name, w1, h1);
}

int defaults_install(progress_fn cb, void * udata) {
  struct stat st;
  int installed = defaults_installed();
  if(installed >= DEFAULTS_VERSION) return 0;
  int fresh = installed == 0;   // No marker (first launch, or deleted to reinstall): missing ones too
  DefaultsCopy dc = { .cb = cb, .udata = udata, .msg = fresh ? "Installing default puzzles" : "Updating default puzzles" };
  char src[STORAGE_MAXPATH], dst[STORAGE_MAXPATH];
  // First pass: what needs copying (decided once: copying changes the answer), total size for the bar
  DIR * d = opendir(DEFAULTS_SRC);
  if(!d) return 1;
  struct dirent * de;
  char * want = NULL;   // Names to copy, each followed by a 0, then a final 0
  size_t wantlen = 0;
  int err = 0;
  while(!err && (de = readdir(d))) {
    if(de->d_name[0] == '.') continue;
    snprintf(src, sizeof(src), "%s/%s", DEFAULTS_SRC, de->d_name);
    snprintf(dst, sizeof(dst), "%s/%s", STORAGE_PUZZLES, de->d_name);
    if(!defaults_wanted(de->d_name, src, dst, fresh) || stat(src, &st) != 0) continue;
    dc.total += st.st_size;
    size_t n = strlen(de->d_name) + 1;
    char * w = realloc(want, wantlen + n + 1);
    if(!w) { err = 2; break; }
    want = w;
    memcpy(want + wantlen, de->d_name, n);
    wantlen += n;
  }
  closedir(d);
  dc.buf = malloc(DEFAULTS_CHUNK);
  if(!dc.buf) err = 2;
  if(!err && want) {
    want[wantlen] = 0;
    defaults_report(&dc, 1);
    for(const char * name = want; !err && *name; name += strlen(name) + 1) {
      snprintf(src, sizeof(src), "%s/%s", DEFAULTS_SRC, name);
      snprintf(dst, sizeof(dst), "%s/%s", STORAGE_PUZZLES, name);
      err = defaults_copyfile(&dc, src, dst);
    }
  }
  free(want);
  free(dc.buf);
  // Only mark done if everything made it; otherwise try again next launch
  if(!err) {
    FILE * f = fopen(DEFAULTS_MARKER, "wb");
    if(f) {
      fprintf(f, "%d\n", DEFAULTS_VERSION);
      fclose(f);
    }
  }
  return err;
}

#include "thumb.h"
#include "cutsample.h"
#include "imageload.h"
#include "imgscale.h"
#include "overlay.h"
#include "picfiles.h"
#include "speedup.h"
#include "swizzle.h"
#include "texutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Decode + shrink into t->plain (worker thread), unless it already holds path. 0 = ok.
static int thumb_decode(Thumb * t, const char * path) {
  if(t->plain && !strcmp(t->plainpath, path)) return 0;
  free(t->plain);
  t->plain = NULL;
  int sw, sh;
  uint8_t * src = imageload_rgb(path, &sw, &sh, NULL, 0);
  if(!src) return 1;
  u16 w, h;
  imgscale_fit(sw, sh, THUMB_MAXW, THUMB_MAXH, &w, &h);
  uint8_t * small = malloc((size_t)w * h * 3);
  int ok = small && !imgscale_area(src, sw, sh, 3, 0, 0, sw, sh, small, w, h);
  imageload_free(src);
  if(!ok) {
    free(small);
    return 1;
  }
  t->plain = small;
  t->plainw = w;
  t->plainh = h;
  snprintf(t->plainpath, sizeof(t->plainpath), "%s", path);
  return 0;
}

// Thumbnail of path with overlay, swizzled into a new texture-sized buffer. NULL on any failure.
static uint32_t * thumb_make(Thumb * t, const char * path, u8 overlay, u16 * outw, u16 * outh) {
  if(thumb_decode(t, path)) return NULL;
  size_t len = (size_t)t->plainw * t->plainh * 3;
  uint8_t * small = malloc(len);
  uint32_t * tex = calloc(THUMB_TEXW * THUMB_TEXH, sizeof(uint32_t));
  if(small && tex) {
    memcpy(small, t->plain, len);
    overlay_apply(overlay, small, t->plainw, t->plainh);
    swizzle_copy_rgb(tex, THUMB_TEXW, 0, 0, small, t->plainw, t->plainh);
    *outw = t->plainw;
    *outh = t->plainh;
  } else {
    free(tex);
    tex = NULL;
  }
  free(small);
  return tex;
}

// Cut sample the size of path's thumbnail, swizzled into a new texture-sized buffer. NULL on
// any failure.
static uint32_t * thumb_makecut(Thumb * t, const char * path, u8 style, u8 whimsy, u8 set, u16 * outw, u16 * outh) {
  if(thumb_decode(t, path)) return NULL;
  uint32_t * lin = malloc(sizeof(uint32_t) * t->plainw * t->plainh);
  uint32_t * tex = calloc(THUMB_TEXW * THUMB_TEXH, sizeof(uint32_t));
  if(lin && tex && !cutsample_draw(style, whimsy, set, t->plainw, t->plainh, lin)) {
    swizzle_copy(tex, THUMB_TEXW, 0, 0, lin, t->plainw, t->plainh, t->plainw);
    *outw = t->plainw;
    *outh = t->plainh;
  } else {
    free(tex);
    tex = NULL;
  }
  free(lin);
  return tex;
}

static void thumb_thread(void * arg) {
  Thumb * t = arg;
  char path[STORAGE_MAXPATH];
  for(;;) {
    LightEvent_Wait(&t->wake);
    for(;;) {
      LightLock_Lock(&t->lock);
      u32 gen = t->want;
      int work = gen != t->taken && t->path[0];
      t->taken = gen;
      u8 overlay = t->overlay;
      u32 cgen = t->cutwant;
      int cwork = cgen != t->cuttaken && t->path[0] && t->cuton;
      t->cuttaken = cgen;
      u8 cstyle = t->cutstyle, cwhimsy = t->cutwhimsy, cset = t->cutset;
      if(work || cwork) {
        memcpy(path, t->path, sizeof(path));
        t->busy = 1;
      }
      LightLock_Unlock(&t->lock);
      if(!work && !cwork) break;
      speedup_begin();
      if(work) {
        u16 w = 0, h = 0;
        uint32_t * buf = thumb_make(t, path, overlay, &w, &h);
        if(!buf) cwork = 0;   // Can't decode it: no picture to put a cut on (don't try twice)
        LightLock_Lock(&t->lock);
        if(t->want == gen) {
          free(t->result);
          t->result = buf;
          t->resultw = w;
          t->resulth = h;
          t->donegen = gen;
          buf = NULL;
        }
        LightLock_Unlock(&t->lock);
        free(buf);  // Stale: a newer request came in while decoding
      }
      // The cut sample before the disk use, so it shows up sooner
      if(cwork) {
        u16 w = 0, h = 0;
        uint32_t * buf = thumb_makecut(t, path, cstyle, cwhimsy, cset, &w, &h);
        LightLock_Lock(&t->lock);
        if(t->cutwant == cgen) {
          free(t->cutresult);
          t->cutresult = buf;
          t->cutresultw = w;
          t->cutresulth = h;
          t->cutdonegen = cgen;
          buf = NULL;
        }
        LightLock_Unlock(&t->lock);
        free(buf);
      }
      if(work) {
        // Then the disk use (path is <puzzles>/<name>)
        const char * name = strrchr(path, '/');
        PicDirs dirs = { STORAGE_PUZZLES, STORAGE_SAVES, STORAGE_CACHE };
        uint64_t bytes = name ? picfiles_size(&dirs, name + 1) : 0;
        LightLock_Lock(&t->lock);
        if(t->want == gen) {
          t->resultbytes = bytes;
          t->bytesgen = gen;
        }
        LightLock_Unlock(&t->lock);
      }
      speedup_end();
      t->busy = 0;
    }
    if(t->quit) return;
  }
}

int thumb_init(Thumb * t) {
  memset(t, 0, sizeof(Thumb));
  LightLock_Init(&t->lock);
  LightEvent_Init(&t->wake, RESET_ONESHOT);
  if(texutil_init(&t->tex, THUMB_TEXW, THUMB_TEXH, 0)) return 1;
  t->hastex = 1;
  // Without the cut texture there's just no cut sample
  t->hascuttex = !texutil_init(&t->cuttex, THUMB_TEXW, THUMB_TEXH, 0);
  // Lower priority than the main thread (bigger number = lower), same core
  s32 prio = 0x30;
  svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  t->thread = threadCreate(thumb_thread, t, THUMB_STACK, prio + 1, -2, false);
  t->running = t->thread != NULL;
  return t->running ? 0 : 1;
}

void thumb_free(Thumb * t) {
  if(t->running) {
    thumb_clear(t, 1);
    t->quit = 1;
    LightEvent_Signal(&t->wake);
    threadJoin(t->thread, U64_MAX);
    threadFree(t->thread);
  }
  if(t->hastex) C3D_TexDelete(&t->tex);
  if(t->hascuttex) C3D_TexDelete(&t->cuttex);
  free(t->result);
  free(t->cutresult);
  free(t->plain);
  memset(t, 0, sizeof(Thumb));
}

void thumb_request(Thumb * t, const char * path, u8 overlay) {
  if(!t->running || !t->hastex) return;
  LightLock_Lock(&t->lock);
  // Same picture already showing: keep it up until the new overlay is ready
  int same = t->state == THUMB_READY && !strcmp(t->path, path);
  // Another picture (maybe another size): the cut sample is redone for it
  int newpic = strcmp(t->path, path) != 0;
  if(newpic) t->cutwant++;
  snprintf(t->path, sizeof(t->path), "%s", path);
  t->overlay = overlay;
  t->want++;
  LightLock_Unlock(&t->lock);
  if(newpic) t->cutstate = THUMB_NONE;
  if(same) t->refresh = 1;
  else {
    t->state = THUMB_LOADING;
    t->hasbytes = 0;
  }
  LightEvent_Signal(&t->wake);
}

void thumb_clear(Thumb * t, int wait) {
  if(!t->running) return;
  LightLock_Lock(&t->lock);
  t->path[0] = 0;
  t->want++;
  free(t->result);
  t->result = NULL;
  t->donegen = 0;
  t->cutwant++;
  free(t->cutresult);
  t->cutresult = NULL;
  t->cutdonegen = 0;
  LightLock_Unlock(&t->lock);
  t->state = THUMB_NONE;
  t->cutstate = THUMB_NONE;
  t->refresh = 0;
  t->hasbytes = 0;
  t->bytesgen = 0;
  while(wait && t->busy) svcSleepThread(2 * 1000 * 1000);  // 2ms
  if(wait) {
    // Idle with nothing to do: the kept picture can go too (its memory is wanted)
    free(t->plain);
    t->plain = NULL;
  }
}

int thumb_poll(Thumb * t) {
  if(t->state == THUMB_NONE) return t->state;
  LightLock_Lock(&t->lock);
  if(t->bytesgen && t->bytesgen == t->want) {
    t->bytes = t->resultbytes;
    t->hasbytes = 1;
    t->bytesgen = 0;
  }
  if(t->cutdonegen && t->cutdonegen == t->cutwant) {
    if(t->cutresult && t->hascuttex) {
      memcpy(t->cuttex.data, t->cutresult, t->cuttex.size);
      C3D_TexFlush(&t->cuttex);
      t->cutsub = texutil_subtex(0, 0, t->cutresultw, t->cutresulth, THUMB_TEXW, THUMB_TEXH);
      t->cutstate = THUMB_READY;
    } else {
      t->cutstate = THUMB_FAILED;
    }
    free(t->cutresult);
    t->cutresult = NULL;
    t->cutdonegen = 0;
  }
  if(t->state != THUMB_LOADING && !t->refresh) {
    LightLock_Unlock(&t->lock);
    return t->state;
  }
  if(t->donegen == t->want) {
    if(t->result) {
      memcpy(t->tex.data, t->result, t->tex.size);
      C3D_TexFlush(&t->tex);
      t->sub = texutil_subtex(0, 0, t->resultw, t->resulth, THUMB_TEXW, THUMB_TEXH);
      free(t->result);
      t->result = NULL;
      t->state = THUMB_READY;
    } else {
      t->state = THUMB_FAILED;
    }
    t->donegen = 0;
    t->refresh = 0;
  }
  LightLock_Unlock(&t->lock);
  return t->state;
}

C2D_Image thumb_image(Thumb * t) {
  return (C2D_Image) { .tex = &t->tex, .subtex = &t->sub };
}

void thumb_cut(Thumb * t, u8 style, u8 whimsy, u8 set) {
  if(!t->running || !t->hascuttex) return;
  LightLock_Lock(&t->lock);
  int same = t->cuton && t->cutstyle == style && t->cutwhimsy == whimsy && t->cutset == set;
  if(!same) {
    t->cuton = 1;
    t->cutstyle = style;
    t->cutwhimsy = whimsy;
    t->cutset = set;
    t->cutwant++;
  }
  LightLock_Unlock(&t->lock);
  // The old sample stays up until the new one is ready
  if(!same) LightEvent_Signal(&t->wake);
}

int thumb_cutready(Thumb * t) {
  return t->cutstate == THUMB_READY && t->state == THUMB_READY;
}

C2D_Image thumb_cutimage(Thumb * t) {
  return (C2D_Image) { .tex = &t->cuttex, .subtex = &t->cutsub };
}

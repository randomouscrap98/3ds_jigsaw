#include "thumb.h"
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
      if(work) {
        memcpy(path, t->path, sizeof(path));
        t->busy = 1;
      }
      LightLock_Unlock(&t->lock);
      if(!work) break;
      u16 w = 0, h = 0;
      speedup_begin();
      uint32_t * buf = thumb_make(t, path, overlay, &w, &h);
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
      // Then the disk use (path is <puzzles>/<name>)
      const char * name = strrchr(path, '/');
      PicDirs dirs = { STORAGE_PUZZLES, STORAGE_SAVES, STORAGE_CACHE };
      uint64_t bytes = name ? picfiles_size(&dirs, name + 1) : 0;
      speedup_end();
      LightLock_Lock(&t->lock);
      if(t->want == gen) {
        t->resultbytes = bytes;
        t->bytesgen = gen;
      }
      t->busy = 0;
      LightLock_Unlock(&t->lock);
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
  free(t->result);
  free(t->plain);
  memset(t, 0, sizeof(Thumb));
}

void thumb_request(Thumb * t, const char * path, u8 overlay) {
  if(!t->running || !t->hastex) return;
  LightLock_Lock(&t->lock);
  // Same picture already showing: keep it up until the new overlay is ready
  int same = t->state == THUMB_READY && !strcmp(t->path, path);
  snprintf(t->path, sizeof(t->path), "%s", path);
  t->overlay = overlay;
  t->want++;
  LightLock_Unlock(&t->lock);
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
  LightLock_Unlock(&t->lock);
  t->state = THUMB_NONE;
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

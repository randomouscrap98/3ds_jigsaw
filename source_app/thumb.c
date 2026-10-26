#include "thumb.h"
#include "imgscale.h"
#include "piecebake.h"
#include "puzzlegfx.h"
#include "swizzle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"  // Implementation is in puzzlegfx.c

// Decode + shrink + swizzle into a new texture-sized buffer. NULL on any failure.
static uint32_t * thumb_decode(const char * path, u16 * outw, u16 * outh) {
  int sw, sh, sc;
  if(!stbi_info(path, &sw, &sh, &sc) || (u64)sw * sh > PGFX_MAXSRCPIXELS) return NULL;
  uint8_t * src = stbi_load(path, &sw, &sh, &sc, 3);
  if(!src) return NULL;
  float s = (float)THUMB_MAXW / sw;
  if((float)THUMB_MAXH / sh < s) s = (float)THUMB_MAXH / sh;
  if(s > 1) s = 1;
  u16 w = sw * s, h = sh * s;
  if(w < 1) w = 1;
  if(h < 1) h = 1;
  uint8_t * small = malloc((size_t)w * h * 3);
  uint32_t * px = malloc(sizeof(uint32_t) * w * h);
  uint32_t * tex = calloc(THUMB_TEXW * THUMB_TEXH, sizeof(uint32_t));
  int ok = small && px && tex && !imgscale_area(src, sw, sh, 3, 0, 0, sw, sh, small, w, h);
  stbi_image_free(src);
  if(ok) {
    for(uint32_t i = 0; i < (uint32_t)w * h; i++) {
      px[i] = PIECEBAKE_RGBA(small[i * 3], small[i * 3 + 1], small[i * 3 + 2], 255);
    }
    swizzle_copy(tex, THUMB_TEXW, 0, 0, px, w, h, w);
  }
  free(small);
  free(px);
  if(!ok) {
    free(tex);
    return NULL;
  }
  *outw = w;
  *outh = h;
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
      if(work) {
        memcpy(path, t->path, sizeof(path));
        t->busy = 1;
      }
      LightLock_Unlock(&t->lock);
      if(!work) break;
      u16 w = 0, h = 0;
      uint32_t * buf = thumb_decode(path, &w, &h);
      LightLock_Lock(&t->lock);
      if(t->want == gen) {
        free(t->result);
        t->result = buf;
        t->resultw = w;
        t->resulth = h;
        t->donegen = gen;
        buf = NULL;
      }
      t->busy = 0;
      LightLock_Unlock(&t->lock);
      free(buf);  // Stale: a newer request came in while decoding
    }
    if(t->quit) return;
  }
}

int thumb_init(Thumb * t) {
  memset(t, 0, sizeof(Thumb));
  LightLock_Init(&t->lock);
  LightEvent_Init(&t->wake, RESET_ONESHOT);
  if(!C3D_TexInit(&t->tex, THUMB_TEXW, THUMB_TEXH, GPU_RGBA8)) return 1;
  t->hastex = 1;
  C3D_TexSetFilter(&t->tex, GPU_LINEAR, GPU_LINEAR);
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
  memset(t, 0, sizeof(Thumb));
}

void thumb_request(Thumb * t, const char * path) {
  if(!t->running || !t->hastex) return;
  LightLock_Lock(&t->lock);
  snprintf(t->path, sizeof(t->path), "%s", path);
  t->want++;
  LightLock_Unlock(&t->lock);
  t->state = THUMB_LOADING;
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
  while(wait && t->busy) svcSleepThread(2 * 1000 * 1000);  // 2ms
}

int thumb_poll(Thumb * t) {
  if(t->state != THUMB_LOADING) return t->state;
  LightLock_Lock(&t->lock);
  if(t->donegen == t->want) {
    if(t->result) {
      memcpy(t->tex.data, t->result, t->tex.size);
      C3D_TexFlush(&t->tex);
      t->sub = (Tex3DS_SubTexture) {
        .width = t->resultw,
        .height = t->resulth,
        .left = 0,
        .top = 1.0f,
        .right = (float)t->resultw / THUMB_TEXW,
        .bottom = 1.0f - (float)t->resulth / THUMB_TEXH,
      };
      free(t->result);
      t->result = NULL;
      t->state = THUMB_READY;
    } else {
      t->state = THUMB_FAILED;
    }
    t->donegen = 0;
  }
  LightLock_Unlock(&t->lock);
  return t->state;
}

C2D_Image thumb_image(Thumb * t) {
  return (C2D_Image) { .tex = &t->tex, .subtex = &t->sub };
}

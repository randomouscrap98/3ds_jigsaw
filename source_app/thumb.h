#ifndef __HEADER_JIGSAW_THUMB__
#define __HEADER_JIGSAW_THUMB__

#include <3ds.h>
#include <citro2d.h>

#include "storage.h"

#define THUMB_STACK (32 * 1024)
#define THUMB_TEXW 512
#define THUMB_TEXH 256
#define THUMB_MAXW 320    // Thumbnail is scaled to fit this (never upscaled)
#define THUMB_MAXH 200

#define THUMB_NONE 0
#define THUMB_LOADING 1
#define THUMB_READY 2
#define THUMB_FAILED 3

// A small picture of one image, decoded on a background thread, with a picture overlay
// (OVERLAY_*) on it. Only the latest request matters: older ones still decoding are thrown
// away when they finish. The worker keeps the last picture it decoded (plain), so asking
// for the same picture with another overlay doesn't decode it again.
typedef struct {
  Thread thread;
  LightLock lock;
  LightEvent wake;
  C3D_Tex tex;
  Tex3DS_SubTexture sub;
  char path[STORAGE_MAXPATH];   // Wanted image; empty = nothing
  u8 overlay;                   // Wanted overlay (also what the main thread last asked for)
  // Worker only (and thumb_clear once the worker is idle): last decode, before the overlay
  char plainpath[STORAGE_MAXPATH];
  uint8_t * plain;
  u16 plainw, plainh;
  u32 want;                     // Generation of the latest request/clear
  u32 taken;                    // Generation the worker last picked up
  u32 donegen;                  // Generation of result (0 = no result)
  uint32_t * result;            // Owned; swizzled THUMB_TEXW x THUMB_TEXH, NULL = failed
  u16 resultw, resulth;
  u8 state;                     // THUMB_*, main thread only
  u8 refresh;                   // Main thread: READY, but a new overlay of it is on the way
  volatile u8 busy;
  volatile u8 quit;
  u8 running;
  u8 hastex;
} Thumb;

// Without a thread or texture, thumbnails are just never shown
int thumb_init(Thumb * t);
void thumb_free(Thumb * t);
// Start making the thumbnail of the image at path with overlay (replaces any earlier request)
void thumb_request(Thumb * t, const char * path, u8 overlay);
// Forget the current thumbnail. wait = block until the worker is idle (so its memory is free)
void thumb_clear(Thumb * t, int wait);
// Call each frame on the main thread; uploads a finished thumbnail. Returns THUMB_*.
int thumb_poll(Thumb * t);
C2D_Image thumb_image(Thumb * t);

#endif

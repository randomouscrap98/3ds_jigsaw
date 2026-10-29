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
// for the same picture with another overlay doesn't decode it again. After the picture, the
// worker adds up the puzzle's disk use (picture + save + caches, picfiles_size).
// Separately, a cut sample (cutsample) the size of the picture can be asked for: its own
// texture, drawn over the picture, so changing the cut settings never redoes the picture.
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
  uint64_t resultbytes;         // Disk use, for generation bytesgen (0 = none yet)
  u32 bytesgen;
  u16 resultw, resulth;
  u8 state;                     // THUMB_*, main thread only
  u8 refresh;                   // Main thread: READY, but a new overlay of it is on the way
  uint64_t bytes;               // Main thread: the puzzle's disk use (valid when hasbytes)
  u8 hasbytes;
  // Cut sample: wanted settings (main thread, under lock), worker generations, result
  C3D_Tex cuttex;
  Tex3DS_SubTexture cutsub;
  u8 cutstyle, cutwhimsy, cutset;
  uint32_t cuttarget;           // Piece count: the sample sits on that puzzle's crop
  u8 cuton;                     // A cut sample has been asked for
  u32 cutwant, cuttaken, cutdonegen;
  uint32_t * cutresult;         // Owned; swizzled THUMB_TEXW x THUMB_TEXH, NULL = failed
  u16 cutresultx, cutresulty, cutresultw, cutresulth;
  u8 cutstate;                  // THUMB_*, main thread only
  Tex3DS_SubTexture cropsub;    // Main thread: the crop of the picture the cut sample covers
  u8 hascuttex;
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
// Ask for the cut sample with these settings over the current (and any later) picture,
// covering the crop a puzzle of about target pieces keeps; asking for the settings already
// wanted does nothing
void thumb_cut(Thumb * t, u8 style, u8 whimsy, u8 set, uint32_t target);
// The cut sample, if one matching the picture is ready (thumb_poll uploads it)
int thumb_cutready(Thumb * t);
C2D_Image thumb_cutimage(Thumb * t);
// The part of the picture under the cut sample (the puzzle's crop); only when thumb_cutready
C2D_Image thumb_cropimage(Thumb * t);

#endif

#ifndef __HEADER_JIGSAW_MUSIC__
#define __HEADER_JIGSAW_MUSIC__

#include <3ds.h>

#include "storage.h"

#define MUSIC_DIR       STORAGE_MUSIC
#define MUSIC_CHANNEL   2               // ndsp channel (sfx uses 0 and 1)
#define MUSIC_BUFS      4
#define MUSIC_FRAMES    4096            // Per buffer; 4 x 4096 = ~370 ms ahead at 44.1 kHz
#define MUSIC_STACK     (64 * 1024)     // minimp3 keeps its scratch on the stack
#define MUSIC_MAXALLOC  (256 * 1024)    // Biggest decoder allocation allowed (a non-MP3 file
                                        // would otherwise be read in whole looking for a frame)
#define MUSIC_MAXNAME   256

// Background music: every .mp3 in MUSIC_DIR, alphabetical, looping the folder. Decoded on
// a thread above the main thread (short bursts, never more than real time) into a few
// ndsp buffers. Files that fail to open or decode are skipped. Everything is a no-op if
// ndsp isn't available or the folder is empty.
typedef struct {
  Thread thread;
  LightEvent wake;      // Signalled every DSP frame, and on quit
  LightLock lock;       // Guards name and busy
  volatile u8 quit;
  u8 running;
  s16 * pcm;            // MUSIC_BUFS * MUSIC_FRAMES stereo frames, linear memory
  ndspWaveBuf wave[MUSIC_BUFS];
  char name[MUSIC_MAXNAME];  // File playing now ("" = nothing)
  u64 busy;             // System ticks spent decoding, total
  u64 busyseen;         // busy at the last music_busy call
} Music;

// ndspok: ndsp was initialized (sfx_init succeeded). Call music_free before ndspExit.
int music_init(Music * m, int ndspok);
void music_free(Music * m);
// Copy the current file name (may be "")
void music_name(Music * m, char * out, size_t len);
// System ticks spent decoding since the last call
u64 music_busy(Music * m);

#endif

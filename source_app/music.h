#ifndef __HEADER_JIGSAW_MUSIC__
#define __HEADER_JIGSAW_MUSIC__

#include <3ds.h>

#include "storage.h"

#define MUSIC_DIR       STORAGE_MUSIC
#define MUSIC_CHANNEL   2               // ndsp channel (sfx uses 0 and 1)
#define MUSIC_BUFS      24
#define MUSIC_FRAMES    2048            // Per buffer; 24 x 2048 = ~1.1 s ahead at 44.1 kHz
                                        // (>= MUSIC_FADE_S, so a fade can play out of it)
#define MUSIC_FADE_S    1.0f            // Fade out when stopped
#define MUSIC_STACK     (64 * 1024)     // minimp3 keeps its scratch on the stack
#define MUSIC_MAXNAME   256

#define MUSIC_OFF     0
#define MUSIC_ORDER   1
#define MUSIC_SHUFFLE 2  // A new random order each start, reshuffled when it runs out

// Background music: every .mp3 / .flac / .ogg in MUSIC_DIR, alphabetical or shuffled,
// looping the folder, while started. Decoded on a thread above the main thread, one small buffer per DSP frame, so
// the work comes in even slices. Stopping frees the decoder (and the file list) at once
// and fades out what's already decoded. Each start picks up at the next file. Files that
// fail to open or decode are skipped. Everything is a no-op if ndsp isn't available or
// the folder is empty.
typedef struct {
  Thread thread;
  LightEvent wake;      // Signalled every DSP frame, and on quit
  LightLock lock;       // Guards name, busy and the requests
  u8 quit, mode, skip;  // Requests to the thread (under lock); mode = MUSIC_OFF etc.
  u8 running;
  float volume;         // Channel gain while playing, 0..1 (under lock)
  uint32_t next;        // Index of the next file to play (thread only)
  s16 * pcm;            // MUSIC_BUFS * MUSIC_FRAMES stereo frames, linear memory
  ndspWaveBuf wave[MUSIC_BUFS];
  char name[MUSIC_MAXNAME];  // File playing now ("" = nothing)
  u64 busy;             // System ticks spent decoding, total (not counting file reads)
  u64 sdbusy;           // System ticks spent in file reads, total (MP3 / FLAC only)
  u64 busyseen, sdseen; // Totals at the last music_busy call
} Music;

// ndspok: ndsp was initialized (sfx_init succeeded). Call music_free before ndspExit.
int music_init(Music * m, int ndspok);
void music_free(Music * m);
// MUSIC_OFF fades out and stops; the others start playing if stopped. Switching between
// in order and shuffled only changes how the next file is picked. Returns at once.
void music_setmode(Music * m, int mode);
// Channel gain 0..1 (fades scale it). Returns at once.
void music_setvolume(Music * m, float volume);
// Go to the next file. Returns at once.
void music_skip(Music * m);
// Copy the current file name (may be "")
void music_name(Music * m, char * out, size_t len);
// System ticks spent decoding and reading files since the last call (Ogg reads count
// as decoding: stb_vorbis reads its file itself)
void music_busy(Music * m, u64 * decode, u64 * sd);

#endif

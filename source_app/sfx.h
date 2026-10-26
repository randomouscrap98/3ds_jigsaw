#ifndef __HEADER_JIGSAW_SFX__
#define __HEADER_JIGSAW_SFX__

#include <3ds.h>

#define SFX_RATE 22050
#define SFX_MERGE 0
#define SFX_COMPLETE 1
#define SFX_COUNT 2

// Tiny sound effects, synthesized at startup by sfxgen (no audio files, no licenses).
// If the DSP can't start (no dspfirm.cdc dumped), everything is silently a no-op.
typedef struct {
  s16 * data[SFX_COUNT];
  ndspWaveBuf wave[SFX_COUNT];
  u8 ok;
} Sfx;

int sfx_init(Sfx * s);
void sfx_free(Sfx * s);
void sfx_play(Sfx * s, int id);

#endif

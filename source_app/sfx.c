#include "sfx.h"
#include "sfxgen.h"

#include <string.h>

static u32 sfx_gen_merge(s16 * out, u32 max) {
  return sfxgen_merge(out, max, SFX_RATE);
}

static u32 sfx_gen_complete(s16 * out, u32 max) {
  return sfxgen_complete(out, max, SFX_RATE);
}

int sfx_init(Sfx * s) {
  memset(s, 0, sizeof(Sfx));
  s->later = -1;
  if(R_FAILED(ndspInit())) return 1;
  s->ok = 1;
  ndspSetOutputMode(NDSP_OUTPUT_STEREO);
  u32 (*gens[SFX_COUNT])(s16 *, u32) = { sfx_gen_merge, sfx_gen_complete };
  const u32 maxsamples = SFX_RATE;  // 1 second each is plenty
  for(int i = 0; i < SFX_COUNT; i++) {
    s->data[i] = linearAlloc(maxsamples * sizeof(s16));
    if(!s->data[i]) continue;
    u32 n = gens[i](s->data[i], maxsamples);
    DSP_FlushDataCache(s->data[i], n * sizeof(s16));
    s->wave[i].data_vaddr = s->data[i];
    s->wave[i].nsamples = n;
    s->wave[i].looping = false;
    s->wave[i].status = NDSP_WBUF_DONE;
    ndspChnReset(i);
    ndspChnSetInterp(i, NDSP_INTERP_LINEAR);
    ndspChnSetRate(i, SFX_RATE);
    ndspChnSetFormat(i, NDSP_FORMAT_MONO_PCM16);
    float mix[12] = { 1.0f, 1.0f };
    ndspChnSetMix(i, mix);
  }
  return 0;
}

void sfx_free(Sfx * s) {
  if(s->ok) {
    for(int i = 0; i < SFX_COUNT; i++) ndspChnWaveBufClear(i);
    ndspExit();
  }
  for(int i = 0; i < SFX_COUNT; i++) {
    if(s->data[i]) linearFree(s->data[i]);
  }
  memset(s, 0, sizeof(Sfx));
}

void sfx_play(Sfx * s, int id) {
  if(!s->ok || id < 0 || id >= SFX_COUNT || !s->data[id]) return;
  // Restart from the beginning if it's still playing
  ndspChnWaveBufClear(id);
  s->wave[id].status = NDSP_WBUF_DONE;
  ndspChnWaveBufAdd(id, &s->wave[id]);
}

void sfx_play_later(Sfx * s, int id, int frames) {
  s->later = id;
  s->laterframes = frames;
}

void sfx_tick(Sfx * s) {
  if(s->later < 0 || --s->laterframes > 0) return;
  sfx_play(s, s->later);
  s->later = -1;
}

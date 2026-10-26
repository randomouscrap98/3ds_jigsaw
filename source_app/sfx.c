#include "sfx.h"

#include <math.h>
#include <string.h>

// A short woody "tock": two decaying sines plus a tiny click of noise
static u32 sfx_gen_merge(s16 * out, u32 max) {
  u32 n = SFX_RATE * 0.09f;
  if(n > max) n = max;
  u32 noise = 0x1234567;
  for(u32 i = 0; i < n; i++) {
    float t = (float)i / SFX_RATE;
    float env = expf(-t * 55);
    float v = sinf(2 * M_PI * 820 * t) * 0.6f + sinf(2 * M_PI * 1640 * t) * 0.25f;
    if(i < SFX_RATE * 0.004f) {
      noise = noise * 1103515245u + 12345u;
      v += ((float)((noise >> 16) & 0x7FFF) / 16384.0f - 1) * 0.5f * (1 - i / (SFX_RATE * 0.004f));
    }
    out[i] = (s16)(v * env * 0.5f * 32767);
  }
  return n;
}

// Rising three note chime
static u32 sfx_gen_complete(s16 * out, u32 max) {
  const float notes[3] = { 1046.5f, 1318.5f, 1568.0f };
  const float step = 0.11f;
  u32 n = SFX_RATE * (step * 2 + 0.5f);
  if(n > max) n = max;
  for(u32 i = 0; i < n; i++) {
    float t = (float)i / SFX_RATE, v = 0;
    for(int k = 0; k < 3; k++) {
      float tk = t - k * step;
      if(tk < 0) continue;
      v += sinf(2 * M_PI * notes[k] * tk) * expf(-tk * 6) * 0.3f;
    }
    out[i] = (s16)(v * 32767 * 0.6f);
  }
  return n;
}

int sfx_init(Sfx * s) {
  memset(s, 0, sizeof(Sfx));
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

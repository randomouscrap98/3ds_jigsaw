#include "sfxgen.h"

#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Raw knocks into v (n samples). shaped = 0 leaves out the attack.
// Returns the peak.
static float sfxgen_knocks(float * v, uint32_t n, uint32_t rate, const SfxClick * c, int shaped) {
  uint32_t seed = 0x1234567;
  float y1[2] = { 0, 0 }, y2[2] = { 0, 0 }, last = 0, peak = 0;
  // Two-pole resonators, one per knock: r sets the ring time, the angle sets the pitch
  float a1[2], a2[2], gain[2];
  for(int k = 0; k < 2; k++) {
    float r = expf(-1.0f / (c->ring * rate));
    a1[k] = 2 * r * cosf(2 * M_PI * c->freq[k] / rate);
    a2[k] = -r * r;
    gain[k] = 1 - r;  // Rough normalization: narrower resonances get louder
  }
  for(uint32_t i = 0; i < n; i++) {
    float t = (float)i / rate;
    seed = seed * 1103515245u + 12345u;
    float white = (float)((seed >> 16) & 0x7FFF) / 16384.0f - 1;
    float noise = white - last;  // High-passed, so the snap is crisp
    last = white;
    float sum = 0;
    for(int k = 0; k < 2; k++) {
      float tk = t - k * c->gap;
      float att = shaped && c->attack > 0 && tk < c->attack ? tk / c->attack : 1;
      float e = tk < 0 ? 0 : noise * att * expf(-tk / c->burst);
      float y = e + a1[k] * y1[k] + a2[k] * y2[k];
      y2[k] = y1[k];
      y1[k] = y;
      sum += c->amp[k] * (y * gain[k] * (1 - c->snap) * 8 + e * c->snap);
    }
    v[i] = sum;
    if(fabsf(sum) > peak) peak = fabsf(sum);
  }
  return peak;
}

uint32_t sfxgen_click(int16_t * out, uint32_t max, uint32_t rate, const SfxClick * c) {
  uint32_t n = rate * (c->gap + c->ring * 6);
  if(n > max) n = max;
  float * v = malloc(sizeof(float) * n);
  if(!v) return 0;
  // Level comes from the unshaped sound, so softening the onset doesn't turn the rest up
  float peak = sfxgen_knocks(v, n, rate, c, 0);
  sfxgen_knocks(v, n, rate, c, 1);
  // Soft clip, and fade out the tail
  const float norm = tanhf(c->drive);
  uint32_t fade = c->fade * rate;
  for(uint32_t i = 0; i < n; i++) {
    float x = peak > 0 ? v[i] / peak : 0;
    float f = fade && i + fade > n ? (float)(n - i) / fade : 1;
    out[i] = (int16_t)(tanhf(x * c->drive) / norm * f * 0.5f * 32767);
  }
  free(v);
  return n;
}

uint32_t sfxgen_merge(int16_t * out, uint32_t max, uint32_t rate) {
  const SfxClick c = SFXGEN_MERGE;
  return sfxgen_click(out, max, rate, &c);
}

uint32_t sfxgen_complete(int16_t * out, uint32_t max, uint32_t rate) {
  const float notes[3] = { 1046.5f, 1318.5f, 1568.0f };
  const float step = 0.11f;
  uint32_t n = rate * (step * 2 + 0.5f);
  if(n > max) n = max;
  for(uint32_t i = 0; i < n; i++) {
    float t = (float)i / rate, v = 0;
    for(int k = 0; k < 3; k++) {
      float tk = t - k * step;
      if(tk < 0) continue;
      v += sinf(2 * M_PI * notes[k] * tk) * expf(-tk * 6) * 0.3f;
    }
    out[i] = (int16_t)(v * 32767 * 0.6f);
  }
  return n;
}

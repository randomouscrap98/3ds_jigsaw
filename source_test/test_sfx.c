#include "test.h"
#include "sfxgen.h"

#include <stdlib.h>
#include <string.h>

void test_sfxgen(void) {
  const uint32_t rate = 22050, max = rate;
  int16_t * buf = malloc(max * sizeof(int16_t));
  uint32_t n = sfxgen_merge(buf, max, rate);
  // About 60 ms, loud enough, and two separate clicks: quiet just before the second one
  CHECK(n > rate * 0.04f && n < rate * 0.1f);
  // Both peaks hit the soft clip ceiling, so compare energy over the first 8 ms of each
  const SfxClick c = SFXGEN_MERGE;
  uint32_t second = rate * c.gap, win = rate * 0.008f;
  double e1 = 0, e2 = 0;
  int gap = 0;
  for(uint32_t i = 0; i < win; i++) {
    e1 += (double)buf[i] * buf[i];
    e2 += (double)buf[second + i] * buf[second + i];
  }
  for(uint32_t i = second - rate * 0.004f; i < second; i++) if(abs(buf[i]) > gap) gap = abs(buf[i]);
  CHECK(e1 > win * 1500.0 * 1500.0 && e2 > e1);
  CHECK(gap < 1000);
  // Limits respected
  CHECK(sfxgen_merge(buf, 10, rate) == 10);
  CHECK(sfxgen_complete(buf, max, rate) > rate / 2);
  free(buf);
}

static void test_wav(const char * path, const int16_t * s, uint32_t n, uint32_t rate, int stereo) {
  FILE * f = fopen(path, "wb");
  if(!f) return;
  uint16_t ch = stereo ? 2 : 1;
  uint32_t bytes = n * 2 * ch, v;
  fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f);
  fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
  uint16_t fmt[2] = { 1, ch }; fwrite(fmt, 2, 2, f);           // PCM
  fwrite(&rate, 4, 1, f); v = rate * 2 * ch; fwrite(&v, 4, 1, f);
  uint16_t blk[2] = { (uint16_t)(2 * ch), 16 }; fwrite(blk, 2, 2, f);
  fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
  for(uint32_t i = 0; i < n; i++) for(int c = 0; c < ch; c++) fwrite(s + i, 2, 1, f);
  fclose(f);
}

// For listening on a PC: merge.wav, complete.wav (22050 Hz mono, like the 3DS), click
// variants merge_a/b/c.wav, and banner.wav (the chime, 44100 Hz stereo for bannertool)
void test_dump_sfx(const char * dir) {
  const uint32_t rate = 22050, max = 44100 * 2;
  int16_t * buf = malloc(max * sizeof(int16_t));
  char path[512];
  snprintf(path, sizeof(path), "%s/merge.wav", dir);
  test_wav(path, buf, sfxgen_merge(buf, max, rate), rate, 0);
  snprintf(path, sizeof(path), "%s/complete.wav", dir);
  test_wav(path, buf, sfxgen_complete(buf, max, rate), rate, 0);
  const SfxClick variants[3] = {
    { .gap = 0.03f, .freq = { 650, 900 }, .amp = { 0.65f, 1.0f }, .burst = 0.0015f, .ring = 0.006f, .snap = 0.3f, .drive = 2.5f, .attack = 0.001f, .fade = 0.01f },
    { .gap = 0.032f, .freq = { 420, 560 }, .amp = { 0.65f, 1.0f }, .burst = 0.0018f, .ring = 0.008f, .snap = 0.25f, .drive = 2.5f, .attack = 0.001f, .fade = 0.01f },
    { .gap = 0.028f, .freq = { 950, 1350 }, .amp = { 0.65f, 1.0f }, .burst = 0.0012f, .ring = 0.004f, .snap = 0.4f, .drive = 2.5f, .attack = 0.001f, .fade = 0.01f },
  };
  for(int v = 0; v < 3; v++) {
    snprintf(path, sizeof(path), "%s/merge_%c.wav", dir, 'a' + v);
    test_wav(path, buf, sfxgen_click(buf, max, rate, variants + v), rate, 0);
  }
  // finish.wav: the last piece's click, then the chime 4 frames later (as main plays them)
  int16_t * mix = calloc(max, sizeof(int16_t));
  uint32_t nc = sfxgen_merge(buf, max, rate), off = rate * 4 / 60;
  memcpy(mix, buf, nc * sizeof(int16_t));
  uint32_t nm = sfxgen_complete(buf, max - off, rate);
  for(uint32_t i = 0; i < nm; i++) {
    int v = mix[off + i] + buf[i];
    mix[off + i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
  }
  snprintf(path, sizeof(path), "%s/finish.wav", dir);
  test_wav(path, mix, off + nm, rate, 0);
  free(mix);
  snprintf(path, sizeof(path), "%s/banner.wav", dir);
  test_wav(path, buf, sfxgen_complete(buf, max, 44100), 44100, 1);
  free(buf);
}

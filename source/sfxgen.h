#ifndef __HEADER_JIGSAW_SFXGEN__
#define __HEADER_JIGSAW_SFXGEN__

#include <stdint.h>

// Sound effects synthesized from scratch (no audio files, no licenses). Each generator
// writes mono 16-bit samples at the given rate into out (at most max) and returns how many.

// A double click ("ca-lick"): two knocks, each a short noise burst rung through a resonant
// filter (a pitched but noisy knock, not a sine tone) plus a bit of the raw snap
typedef struct {
  float gap;          // Seconds between the two knocks
  float freq[2];      // Resonance of each knock (Hz)
  float amp[2];       // Loudness of each knock
  float burst;        // Noise burst decay time constant (s)
  float ring;         // Resonance decay time constant (s)
  float snap;         // How much raw noise is mixed in (0..1)
  float drive;        // Soft clipping amount (higher = harder)
  float attack;       // Each knock fades in over this long (s), so it doesn't start harsh
  float fade;         // The whole sound fades out over its last this long (s)
} SfxClick;

#define SFXGEN_MERGE { .gap = 0.028f, .freq = { 950, 1350 }, .amp = { 0.65f, 1.0f }, \
  .burst = 0.0012f, .ring = 0.004f, .snap = 0.4f, .drive = 2.5f, \
  .attack = 0.001f, .fade = 0.01f }

uint32_t sfxgen_click(int16_t * out, uint32_t max, uint32_t rate, const SfxClick * c);
// Merge: SFXGEN_MERGE
uint32_t sfxgen_merge(int16_t * out, uint32_t max, uint32_t rate);
// Complete: rising three note chime
uint32_t sfxgen_complete(int16_t * out, uint32_t max, uint32_t rate);

#endif

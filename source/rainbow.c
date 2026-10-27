#include "rainbow.h"

#include <stdlib.h>

// Luma weights (Rec. 601, on the stored values like most image code)
#define RAINBOW_WR 0.299f
#define RAINBOW_WG 0.587f
#define RAINBOW_WB 0.114f

// Corner colors for the corner modes: top left, top right, bottom left, bottom right. Only
// their hue matters (each is scaled to full strength).
static const float rainbow_palettes[RAINBOW_PALETTES][4][3] = {
  { { 0.25f, 0.25f, 1.0f }, { 1.0f, 0.2f, 0.55f }, { 0.2f, 1.0f, 0.45f }, { 1.0f, 0.95f, 0.3f } },  // Blue pink green yellow
  { { 0.55f, 0.2f, 0.9f }, { 1.0f, 0.15f, 0.25f }, { 1.0f, 0.55f, 0.1f }, { 1.0f, 0.85f, 0.2f } },  // Sunset: purple red orange gold
  { { 0.1f, 0.2f, 0.9f }, { 0.6f, 0.3f, 1.0f }, { 0.1f, 0.8f, 0.7f }, { 0.4f, 1.0f, 0.6f } },       // Ocean: blue violet teal mint
  { { 0.2f, 0.6f, 0.2f }, { 0.75f, 0.85f, 0.2f }, { 0.1f, 0.6f, 0.6f }, { 0.85f, 0.45f, 0.15f } },  // Forest: green olive teal rust
  { { 1.0f, 0.15f, 0.15f }, { 1.0f, 0.9f, 0.1f }, { 0.15f, 0.3f, 1.0f }, { 0.15f, 0.85f, 0.25f } }, // Primary: red yellow blue green
  { { 1.0f, 0.4f, 0.7f }, { 0.6f, 0.4f, 1.0f }, { 0.3f, 0.85f, 1.0f }, { 1.0f, 0.75f, 0.35f } },    // Candy: pink lilac sky peach
};

// Shift direction for a color: minus its own luma (so adding it changes no brightness)
static void rainbow_chroma(const float c[3], float d[3]) {
  float y = RAINBOW_WR * c[0] + RAINBOW_WG * c[1] + RAINBOW_WB * c[2];
  for(int i = 0; i < 3; i++) d[i] = c[i] - y;
}

static float rainbow_big(const float d[3]) {
  float big = 0;
  for(int i = 0; i < 3; i++) {
    float a = d[i] < 0 ? -d[i] : d[i];
    if(a > big) big = a;
  }
  return big;
}

// Direction of the shift for a hue: the saturated color minus its own luma (so adding it
// changes no brightness), scaled so the biggest channel change is 1
static void rainbow_dir(float hue, float d[3]) {
  float h = hue / 60.0f;
  int k = (int)h;
  float f = h - k, c[3];
  switch(k % 6) {
    case 0: c[0] = 1; c[1] = f; c[2] = 0; break;
    case 1: c[0] = 1 - f; c[1] = 1; c[2] = 0; break;
    case 2: c[0] = 0; c[1] = 1; c[2] = f; break;
    case 3: c[0] = 0; c[1] = 1 - f; c[2] = 1; break;
    case 4: c[0] = f; c[1] = 0; c[2] = 1; break;
    default: c[0] = 1; c[1] = 0; c[2] = 1 - f; break;
  }
  rainbow_chroma(c, d);
  float big = rainbow_big(d);
  for(int i = 0; i < 3; i++) d[i] /= big;
}

static void rainbow_cornerdirs(const float corners[4][3], float cdirs[4][3]) {
  for(int k = 0; k < 4; k++) {
    rainbow_chroma(corners[k], cdirs[k]);
    float big = rainbow_big(cdirs[k]);
    for(int i = 0; i < 3; i++) cdirs[k][i] /= big;
  }
}

// Corner mode direction at (u, v) in 0..1: the corner colors' directions blended, each
// scaled so its biggest channel change is 1. renorm: the blend scaled up the same way (the
// middle, where they cancel, gets as much color as the corners)
// (cdirs: the corners' directions, from rainbow_cornerdirs)
static void rainbow_cornerdir(const float cdirs[4][3], float u, float v, int renorm, float d[3]) {
  float wt[4] = { (1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v };
  for(int i = 0; i < 3; i++) d[i] = wt[0] * cdirs[0][i] + wt[1] * cdirs[1][i] + wt[2] * cdirs[2][i] + wt[3] * cdirs[3][i];
  float big = rainbow_big(d);
  if(renorm && big > 0) for(int i = 0; i < 3; i++) d[i] /= big;
}

void rainbow_apply(uint8_t * rgb, uint32_t w, uint32_t h, int mode, int palette, float strength, float minlight, float mindark) {
  if(!w || !h) return;
  float cdirs[4][3];
  rainbow_cornerdirs(rainbow_palettes[(unsigned)palette % RAINBOW_PALETTES], cdirs);
  // One direction per diagonal (x + y)
  uint32_t nd = w + h - 1;
  float (*dirs)[3] = malloc(sizeof(*dirs) * nd);
  if(!dirs) return;
  // Hue by the share of the picture before that diagonal (the middle ones are longest), so
  // every hue covers about the same area
  double total = (double)w * h, before = 0;
  for(uint32_t i = 0; i < nd; i++) {
    uint32_t x0 = i < h ? 0 : i - h + 1, x1 = i < w ? i : w - 1;
    double len = x1 - x0 + 1;
    rainbow_dir(RAINBOW_HUESPAN * (float)((before + len / 2) / total), dirs[i]);
    before += len;
  }
  for(uint32_t y = 0; y < h; y++) {
    uint8_t * p = rgb + (size_t)y * w * 3;
    for(uint32_t x = 0; x < w; x++, p += 3) {
      float cd[3];
      const float * d = dirs[x + y];
      if(mode != RAINBOW_DIAGONAL) {
        rainbow_cornerdir(cdirs, w > 1 ? (float)x / (w - 1) : 0.5f, h > 1 ? (float)y / (h - 1) : 0.5f, mode == RAINBOW_CORNERS_FULL, cd);
        d = cd;
      }
      float v[3] = { p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f };
      // Biggest shift that fits without leaving 0..1
      float fit = strength;
      for(int i = 0; i < 3; i++) {
        float room = d[i] > 0 ? (1 - v[i]) / d[i] : d[i] < 0 ? v[i] / -d[i] : strength;
        if(room < fit) fit = room;
      }
      // Floor by which end the pixel is near (only matters near the ends: in the middle
      // the shift fits, so there's no seam at half brightness)
      float luma = RAINBOW_WR * v[0] + RAINBOW_WG * v[1] + RAINBOW_WB * v[2];
      float minimum = luma < 0.5f ? mindark : minlight;
      float a = fit < minimum ? minimum : fit, lo = 1, hi = 0;
      for(int i = 0; i < 3; i++) {
        v[i] += a * d[i];
        if(v[i] < lo) lo = v[i];
        if(v[i] > hi) hi = v[i];
      }
      // Too little room: move toward the middle (equal on every channel: no color change)
      float t = hi > 1 ? 1 - hi : lo < 0 ? -lo : 0;
      for(int i = 0; i < 3; i++) {
        float c = (v[i] + t) * 255.0f + 0.5f;
        p[i] = c < 0 ? 0 : c > 255 ? 255 : (uint8_t)c;
      }
    }
  }
  free(dirs);
}

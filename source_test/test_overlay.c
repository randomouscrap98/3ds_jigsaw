#include "test.h"
#include "overlay.h"

#include <stdlib.h>
#include <string.h>

static float overlay_luma(const uint8_t * p) {
  return (0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) / 255.0f;
}

// One color everywhere, then the overlay
static uint8_t * overlay_flat(uint32_t w, uint32_t h, uint8_t v, uint8_t overlay) {
  uint8_t * img = malloc((size_t)w * h * 3);
  memset(img, v, (size_t)w * h * 3);
  overlay_apply(overlay, img, w, h);
  return img;
}

void test_overlay(void) {
  const uint32_t w = 64, h = 48;
  // None: untouched
  uint8_t * img = overlay_flat(w, h, 77, OVERLAY_NONE);
  int same = 1;
  for(size_t i = 0; i < (size_t)w * h * 3; i++) if(img[i] != 77) same = 0;
  CHECK(same);
  free(img);

  // Hologram on mid grey: brightness kept (rounding only), color added
  img = overlay_flat(w, h, 128, OVERLAY_HOLOGRAM);
  int kept = 1, colored = 0;
  for(size_t i = 0; i < (size_t)w * h; i++) {
    const uint8_t * p = img + i * 3;
    float d = overlay_luma(p) - 128 / 255.0f;
    if(d > 0.01f || d < -0.01f) kept = 0;
    if(abs(p[0] - p[1]) > 20 || abs(p[1] - p[2]) > 20) colored++;
  }
  CHECK(kept && colored > (int)(w * h / 2));
  // Corners: blue top left, pink top right, green bottom left, yellow bottom right
  const uint8_t * tl = img, * tr = img + (w - 1) * 3, * bl = img + (size_t)(h - 1) * w * 3, * br = bl + (w - 1) * 3;
  CHECK(tl[2] > tl[0] && tl[2] > tl[1]);
  CHECK(tr[0] > tr[1] && tr[0] > tr[2]);
  CHECK(bl[1] > bl[0] && bl[1] > bl[2]);
  CHECK(br[0] > br[2] && br[1] > br[2]);
  free(img);

  // White turns pastel (some color, still light); black gets a little (still dark)
  img = overlay_flat(w, h, 255, OVERLAY_HOLOGRAM);
  CHECK(overlay_luma(img) > 0.65f && img[2] - img[0] > 30);   // Blue corner: (169, 169, 255)
  free(img);
  img = overlay_flat(w, h, 0, OVERLAY_HOLOGRAM);
  CHECK(overlay_luma(img) < 0.12f && img[2] > 10);
  free(img);

  // Rainbow: gentle, brightness kept on grey; red at the top left, purple at the bottom right
  img = overlay_flat(w, h, 128, OVERLAY_RAINBOW);
  kept = 1;
  for(size_t i = 0; i < (size_t)w * h; i++) {
    float d = overlay_luma(img + i * 3) - 128 / 255.0f;
    if(d > 0.01f || d < -0.01f) kept = 0;
  }
  CHECK(kept);
  tl = img;
  br = img + ((size_t)(h - 1) * w + w - 1) * 3;
  CHECK(tl[0] > tl[1] && tl[0] > tl[2]);
  CHECK(br[0] > br[1] && br[2] > br[1]);
  free(img);
}

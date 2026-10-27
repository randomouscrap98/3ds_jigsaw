// Rainbow wash preview: rainbow_preview OUTDIR IMAGE MODE PALETTE [strength minlight mindark]...
// MODE: 0 diagonal, 1 corners, 2 corners at full strength in the middle (RAINBOW_*).
// PALETTE: corner colors, 0..RAINBOW_PALETTES-1.
// Writes OUTDIR/rainbow_<n>.png for each (strength, minlight, mindark) (default: a few), plus
// OUTDIR/rainbow_all.png: the original and every variant at half size, side by side.

#include "rainbow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define COLS 3

// 2x2 box average into dst (dw x dh) at (ox, oy) in a canvas cw wide
static void half(const uint8_t * src, int w, int h, uint8_t * canvas, int cw, int ox, int oy) {
  for(int y = 0; y < h / 2; y++) {
    for(int x = 0; x < w / 2; x++) {
      for(int c = 0; c < 3; c++) {
        int s = src[((2 * y) * w + 2 * x) * 3 + c] + src[((2 * y) * w + 2 * x + 1) * 3 + c] +
                src[((2 * y + 1) * w + 2 * x) * 3 + c] + src[((2 * y + 1) * w + 2 * x + 1) * 3 + c];
        canvas[((oy + y) * cw + ox + x) * 3 + c] = (s + 2) / 4;
      }
    }
  }
}

int main(int argc, char ** argv) {
  if(argc < 5) {
    fprintf(stderr, "usage: %s OUTDIR IMAGE MODE PALETTE [strength minlight mindark]...\n", argv[0]);
    return 1;
  }
  int w, h, comp;
  uint8_t * img = stbi_load(argv[2], &w, &h, &comp, 3);
  if(!img) {
    fprintf(stderr, "can't load %s\n", argv[2]);
    return 1;
  }
  float defaults[][3] = { { 0.55f, 0.3f, 0.3f }, { 0.55f, 0.3f, 0.2f }, { 0.55f, 0.3f, 0.1f }, { 0.55f, 0.3f, 0.05f }, { 0.55f, 0.3f, 0 } };
  int mode = atoi(argv[3]), palette = atoi(argv[4]), n = (argc - 5) / 3;
  float (*params)[3] = n ? malloc(sizeof(*params) * n) : defaults;
  if(n) for(int i = 0; i < n; i++) {
    params[i][0] = atof(argv[5 + 3 * i]);
    params[i][1] = atof(argv[6 + 3 * i]);
    params[i][2] = atof(argv[7 + 3 * i]);
  }
  else n = sizeof(defaults) / sizeof(defaults[0]);
  int pw = w / 2, ph = h / 2, rows = (n + 1 + COLS - 1) / COLS, cw = pw * COLS, ch = ph * rows;
  uint8_t * canvas = calloc((size_t)cw * ch * 3, 1);
  uint8_t * work = malloc((size_t)w * h * 3);
  half(img, w, h, canvas, cw, 0, 0);
  char path[1024];
  for(int i = 0; i < n; i++) {
    memcpy(work, img, (size_t)w * h * 3);
    rainbow_apply(work, w, h, mode, palette, params[i][0], params[i][1], params[i][2]);
    snprintf(path, sizeof(path), "%s/rainbow_%d.png", argv[1], i + 1);
    stbi_write_png(path, w, h, 3, work, w * 3);
    printf("%s: strength %.2f light %.2f dark %.2f\n", path, params[i][0], params[i][1], params[i][2]);
    half(work, w, h, canvas, cw, ((i + 1) % COLS) * pw, ((i + 1) / COLS) * ph);
  }
  snprintf(path, sizeof(path), "%s/rainbow_all.png", argv[1]);
  stbi_write_png(path, cw, ch, 3, canvas, cw * 3);
  printf("%s: original, then 1..%d\n", path, n);
  return 0;
}

// Host tool: shrink an image so its long side is at most MAXSIDE and write it out as a
// lossless PNG, or as a JPEG if out ends in .jpg (quality PRESCALE_JPGQ, 4:4:4).
// Uses the same area-average scaler as the 3DS (source/imgscale.c). Never upscales.
// Near-black borders (like museum photos shot against black) are trimmed first, plus a
// small inset so no dark fringe survives.
//   prescale <maxside> <in> <out.png|out.jpg>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PRESCALE_JPGQ 95
#define PRESCALE_BLACK 12        // A border row/column has mean brightness below this
#define PRESCALE_INSET 0.005f    // Extra trim past the border, fraction of the image size

// Mean brightness of n pixels starting at (x, y), stepping (dx, dy)
static float prescale_line(const uint8_t * p, int w, int x, int y, int dx, int dy, int n) {
  float s = 0;
  for(int i = 0; i < n; i++) {
    const uint8_t * q = p + ((size_t)(y + dy * i) * w + x + dx * i) * 3;
    s += (q[0] + q[1] + q[2]) / 3.0f;
  }
  return s / n;
}

#include "imgscale.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main(int argc, char ** argv) {
  if(argc != 4) {
    fprintf(stderr, "usage: %s <maxside> <in> <out.png|out.jpg>\n", argv[0]);
    return 2;
  }
  int maxside = atoi(argv[1]), w, h, n;
  uint8_t * src = stbi_load(argv[2], &w, &h, &n, 3);
  if(!src) {
    fprintf(stderr, "%s: %s\n", argv[2], stbi_failure_reason());
    return 1;
  }
  // Trim black borders (never more than a quarter per side), then the inset if any was found
  int t = 0, b = 0, l = 0, r = 0;
  while(t < h / 4 && prescale_line(src, w, 0, t, 1, 0, w) < PRESCALE_BLACK) t++;
  while(b < h / 4 && prescale_line(src, w, 0, h - 1 - b, 1, 0, w) < PRESCALE_BLACK) b++;
  while(l < w / 4 && prescale_line(src, w, l, 0, 0, 1, h) < PRESCALE_BLACK) l++;
  while(r < w / 4 && prescale_line(src, w, w - 1 - r, 0, 0, 1, h) < PRESCALE_BLACK) r++;
  if(t + b + l + r > 0) {
    int ix = (int)(w * PRESCALE_INSET + 0.5f), iy = (int)(h * PRESCALE_INSET + 0.5f);
    t += iy; b += iy; l += ix; r += ix;
  }
  int cw = w - l - r, ch = h - t - b;
  float s = (float)maxside / (cw > ch ? cw : ch);
  if(s > 1) s = 1;
  int dw = (int)(cw * s + 0.5f), dh = (int)(ch * s + 0.5f);
  uint8_t * dst = malloc((size_t)dw * dh * 3);
  if(!dst || imgscale_area(src, w, h, 3, l, t, cw, ch, dst, dw, dh)) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  const char * ext = strrchr(argv[3], '.');
  int ok;
  if(ext && (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg"))) {
    ok = stbi_write_jpg(argv[3], dw, dh, 3, dst, PRESCALE_JPGQ);
  } else {
    stbi_write_png_compression_level = 9;
    ok = stbi_write_png(argv[3], dw, dh, 3, dst, dw * 3);
  }
  if(!ok) {
    fprintf(stderr, "can't write %s\n", argv[3]);
    return 1;
  }
  printf("%s: %dx%d, trimmed t%d b%d l%d r%d -> %dx%d\n", argv[3], w, h, t, b, l, r, dw, dh);
  free(dst);
  stbi_image_free(src);
  return 0;
}

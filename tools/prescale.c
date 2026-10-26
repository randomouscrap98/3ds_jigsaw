// Host tool: shrink an image so its long side is at most MAXSIDE and write it out as a
// lossless PNG, or as a JPEG if out ends in .jpg (quality PRESCALE_JPGQ, 4:4:4).
// Uses the same area-average scaler as the 3DS (source/imgscale.c). Never upscales.
//   prescale <maxside> <in> <out.png|out.jpg>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PRESCALE_JPGQ 95

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
  float s = (float)maxside / (w > h ? w : h);
  if(s > 1) s = 1;
  int dw = (int)(w * s + 0.5f), dh = (int)(h * s + 0.5f);
  uint8_t * dst = malloc((size_t)dw * dh * 3);
  if(!dst || imgscale_area(src, w, h, 3, 0, 0, w, h, dst, dw, dh)) {
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
  printf("%s: %dx%d -> %dx%d\n", argv[3], w, h, dw, dh);
  free(dst);
  stbi_image_free(src);
  return 0;
}

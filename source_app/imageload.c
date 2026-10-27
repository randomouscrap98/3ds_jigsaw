#include "imageload.h"

#include <stdio.h>

// The one stb_image implementation in the app
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define IMAGELOAD_ERR(...) do { if(err) snprintf(err, errlen, __VA_ARGS__); } while(0)

uint8_t * imageload_rgb(const char * path, int * w, int * h, char * err, size_t errlen) {
  int comp;
  if(!stbi_info(path, w, h, &comp)) {
    IMAGELOAD_ERR("Can't read image: %s", stbi_failure_reason());
    return NULL;
  }
  if((uint64_t)*w * *h > (uint64_t)IMAGELOAD_MAXSIDE * IMAGELOAD_MAXSIDE) {
    IMAGELOAD_ERR("Image too big (%dx%d). Max about %d x %d", *w, *h, IMAGELOAD_MAXSIDE, IMAGELOAD_MAXSIDE);
    return NULL;
  }
  uint8_t * px = stbi_load(path, w, h, &comp, 3);
  if(!px) IMAGELOAD_ERR("Can't decode image: %s", stbi_failure_reason());
  return px;
}

int imageload_check(const char * path) {
  int w, h, comp;
  return stbi_info(path, &w, &h, &comp) && (uint64_t)w * h <= (uint64_t)IMAGELOAD_MAXSIDE * IMAGELOAD_MAXSIDE;
}

void imageload_free(uint8_t * px) {
  stbi_image_free(px);
}

#ifndef __HEADER_JIGSAW_IMAGELOAD__
#define __HEADER_JIGSAW_IMAGELOAD__

#include <stddef.h>
#include <stdint.h>

#define IMAGELOAD_MAXSIDE 2100  // Images over about this many pixels squared are refused (decode memory)

// Decode a jpg/png/bmp file to tightly packed RGB (3 bytes per pixel). Returns the pixels
// (free with imageload_free) or NULL, with a human readable reason in err if it's not NULL.
uint8_t * imageload_rgb(const char * path, int * w, int * h, char * err, size_t errlen);
void imageload_free(uint8_t * px);
// 1 if path looks like a picture imageload_rgb would take (header and size only, no decode)
int imageload_check(const char * path);

#endif

#ifndef __HEADER_JIGSAW_IMGSCALE__
#define __HEADER_JIGSAW_IMGSCALE__

#include <stdint.h>

// Area-average (box filter) downscale of a region of an 8-bit image with comp channels.
// The source region (sx, sy, sw, sh) may be fractional. Meant for shrinking: dw <= sw
// and dh <= sh give proper averaging; enlarging works but is just blocky.
// dst must hold dw * dh * comp bytes. Returns 0 on success, nonzero if out of memory.
int imgscale_area(const uint8_t * src, uint32_t srcw, uint32_t srch, int comp,
    float sx, float sy, float sw, float sh, uint8_t * dst, uint32_t dw, uint32_t dh);

#endif

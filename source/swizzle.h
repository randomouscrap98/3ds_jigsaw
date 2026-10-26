#ifndef __HEADER_JIGSAW_SWIZZLE__
#define __HEADER_JIGSAW_SWIZZLE__

#include <stdint.h>

// The 3DS GPU stores textures in 8x8 tiles, each tile in Morton (Z) order.
// Copy a linear w x h block of 32-bit pixels into a tiled texture of width texw
// (multiple of 8) at (dx, dy). Row 0 of src is the top of the image.
void swizzle_copy(uint32_t * tex, uint32_t texw, uint32_t dx, uint32_t dy,
    const uint32_t * src, uint32_t w, uint32_t h, uint32_t srcstride);
// Offset (in pixels) of texture pixel (x, y)
uint32_t swizzle_offset(uint32_t texw, uint32_t x, uint32_t y);

#endif

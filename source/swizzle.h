#ifndef __HEADER_JIGSAW_SWIZZLE__
#define __HEADER_JIGSAW_SWIZZLE__

#include <stdint.h>

// Pack a pixel the way the 3DS GPU wants GPU_RGBA8 (bytes in memory: A B G R)
#define SWIZZLE_RGBA(r, g, b, a) \
  (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

// The 3DS GPU stores textures in 8x8 tiles, each tile in Morton (Z) order.
// Copy a linear w x h block of 32-bit pixels into a tiled texture of width texw
// (multiple of 8) at (dx, dy). Row 0 of src is the top of the image.
void swizzle_copy(uint32_t * tex, uint32_t texw, uint32_t dx, uint32_t dy,
    const uint32_t * src, uint32_t w, uint32_t h, uint32_t srcstride);
// Same, from a tightly packed w x h RGB (3 bytes per pixel) image; pixels come out opaque
void swizzle_copy_rgb(uint32_t * tex, uint32_t texw, uint32_t dx, uint32_t dy,
    const uint8_t * rgb, uint32_t w, uint32_t h);
// Offset (in pixels) of texture pixel (x, y)
uint32_t swizzle_offset(uint32_t texw, uint32_t x, uint32_t y);

#endif

#ifndef __HEADER_JIGSAW_ETC1__
#define __HEADER_JIGSAW_ETC1__

#include "progress.h"

#include <stdint.h>

// ETC1 block compression, for Max piece pages (GPU_ETC1A4, 1 byte per pixel).
// A block is 4x4 pixels in 64 bits, as the ETC1 spec lays it out (bit 63 = first bit).

// rgb: 16 pixels, 3 bytes each, row by row (index y * 4 + x). quality 0 = fast (base colors from
// the averages), 1 = also refits each base color to its chosen modifiers once.
uint64_t etc1_encode(const uint8_t * rgb, int quality);
// Back to 16 pixels of RGB, row by row
void etc1_decode(uint64_t block, uint8_t * rgb);

// A w x h page (multiples of 8) of SWIZZLE_RGBA pixels (row by row, top first) into a GPU_ETC1A4
// texture (w * h bytes), laid out as the 3DS reads it (checked against tex3ds / citro3d / Azahar):
// 8x8 tiles row by row, in each 2x2 blocks (top-left, top-right, bottom-left, bottom-right), a
// block = 8 bytes of 4-bit alpha (pixel (x, y) at nibble x * 4 + y of a little endian u64) then the
// ETC1 block as a little endian u64. Fully transparent blocks are skipped (zeroed). pr hears msg
// done0 + tile row of total once per tile row.
void etc1a4_encode_page(const uint32_t * rgba, uint32_t w, uint32_t h, uint8_t * out, int quality,
    const Progress * pr, const char * msg, int done0, int total);
// Offset of the 16-byte block holding pixel (x, y) in such a texture (for tests and readers)
uint32_t etc1a4_blockoffset(uint32_t w, uint32_t x, uint32_t y);

#endif

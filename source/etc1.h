#ifndef __HEADER_JIGSAW_ETC1__
#define __HEADER_JIGSAW_ETC1__

#include <stdint.h>

// ETC1 block compression (experiment: a possible 1 byte/pixel "Super High" piece format as ETC1A4).
// A block is 4x4 pixels in 64 bits, as the ETC1 spec lays it out (bit 63 = first bit; the 3DS
// stores the u64 little endian and orders blocks in its own tiling: not handled here).

// rgb: 16 pixels, 3 bytes each, row by row (index y * 4 + x). quality 0 = fast (base colors from
// the averages), 1 = also refits each base color to its chosen modifiers once.
uint64_t etc1_encode(const uint8_t * rgb, int quality);
// Back to 16 pixels of RGB, row by row
void etc1_decode(uint64_t block, uint8_t * rgb);

#endif

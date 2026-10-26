#ifndef __HEADER_JIGSAW_TEXUTIL__
#define __HEADER_JIGSAW_TEXUTIL__

#include <citro2d.h>

// Create an RGBA8 texture with linear filtering (clamp = also clamp to edge, for atlases
// and anything drawn scaled). Returns 0 on success.
int texutil_init(C3D_Tex * t, uint16_t w, uint16_t h, int clamp);
// Subtexture for the w x h region at pixel (x, y) of a texw x texh texture whose data was
// filled with swizzle_copy (row 0 = top of the image)
Tex3DS_SubTexture texutil_subtex(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t texw, uint16_t texh);

#endif

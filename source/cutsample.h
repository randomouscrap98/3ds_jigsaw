#ifndef __HEADER_JIGSAW_CUTSAMPLE__
#define __HEADER_JIGSAW_CUTSAMPLE__

#include <stdint.h>

// A small example cut (about CUTSAMPLE_PIECES pieces, grid shaped like the picture) drawn as
// lines on a transparent w x h image, to lay over the picture so the cut settings can be
// seen. Not the puzzle's real cut: same style, whimsies and shape set, fixed seed.

#define CUTSAMPLE_PIECES 100
#define CUTSAMPLE_SEED 0x5A3D1E      // Same sample every time for the same settings
#define CUTSAMPLE_LINE 0.7f          // Dark line half width (pixels)
#define CUTSAMPLE_HALO 1.8f          // Light halo out to this distance (pixels)
#define CUTSAMPLE_LINECOLOR 30, 30, 30
#define CUTSAMPLE_LINEALPHA 230
#define CUTSAMPLE_HALOCOLOR 255, 255, 255
#define CUTSAMPLE_HALOALPHA 110

// The part of a w x h picture a puzzle of about target pieces keeps (layout_crop's centered
// crop), in whole pixels; the sample goes over that
void cutsample_crop(uint16_t w, uint16_t h, uint32_t target, uint16_t * x, uint16_t * y, uint16_t * cw, uint16_t * ch);
// Draws into px (w x h, SWIZZLE_RGBA packing, row 0 at the top; fully overwritten). The cut
// is stretched to fill it exactly (its grid is close to w x h in shape). Returns 0 on success.
int cutsample_draw(uint8_t style, uint8_t whimsy, uint8_t set, uint16_t w, uint16_t h, uint32_t * px);

#endif

#ifndef __HEADER_JIGSAW_PIECEBAKE__
#define __HEADER_JIGSAW_PIECEBAKE__

#include <stdint.h>

#define PIECEBAKE_SUBROWS 4      // Vertical antialiasing samples per pixel
#define PIECEBAKE_MAXCROSS 64
#define PIECEBAKE_OUTLINE 0.45f  // How much to darken the outline


// Rasterizes piece tiles. Holds scratch space so baking hundreds of tiles doesn't
// reallocate each time.
typedef struct {
  float * cover;        // Coverage (0..1) of the tile being baked
  uint16_t maxw, maxh;  // Biggest tile
} PieceBaker;

int piecebake_init(PieceBaker * pb, uint16_t maxw, uint16_t maxh);
void piecebake_free(PieceBaker * pb);
// Bake one w x h tile (at most maxw x maxh). outline: closed polygon (npts xy pairs) in
// tile pixel coordinates. img: RGB (3 bytes per pixel) puzzle image, imgw x imgh. (ox, oy):
// image position of the tile's top-left pixel (can be negative). out: w * h RGBA
// (SWIZZLE_RGBA), straight alpha. Pixels outside the piece keep the image color with
// alpha 0 so filtering doesn't create dark fringes.
void piecebake_tile(PieceBaker * pb, const float * outline, int npts,
    const uint8_t * img, uint32_t imgw, uint32_t imgh, int32_t ox, int32_t oy, uint16_t w, uint16_t h,
    uint32_t * out);

#endif

#ifndef __HEADER_JIGSAW_PIECEBAKE__
#define __HEADER_JIGSAW_PIECEBAKE__

#include <stdint.h>

#define PIECEBAKE_SUBROWS 4      // Vertical antialiasing samples per pixel
#define PIECEBAKE_MAXCROSS 64
#define PIECEBAKE_OUTLINE 0.45f  // How much to darken the outline

// Pack a pixel the way the 3DS GPU wants GPU_RGBA8 (bytes in memory: A B G R)
#define PIECEBAKE_RGBA(r, g, b, a) \
  (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

// Rasterizes piece tiles. Holds scratch space so baking hundreds of tiles doesn't
// reallocate each time.
typedef struct {
  float * cover;        // tile * tile coverage (0..1)
  uint16_t tile;
} PieceBaker;

int piecebake_init(PieceBaker * pb, uint16_t tile);
void piecebake_free(PieceBaker * pb);
// Bake one tile. outline: closed polygon (npts xy pairs) in tile pixel coordinates.
// img: RGB (3 bytes per pixel) puzzle image, imgw x imgh. (ox, oy): image position of
// the tile's top-left pixel (can be negative). out: tile * tile RGBA (PIECEBAKE_RGBA),
// straight alpha. Pixels outside the piece keep the image color with alpha 0 so
// filtering doesn't create dark fringes.
void piecebake_tile(PieceBaker * pb, const float * outline, int npts,
    const uint8_t * img, uint32_t imgw, uint32_t imgh, int32_t ox, int32_t oy, uint32_t * out);

#endif

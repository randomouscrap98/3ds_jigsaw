#ifndef __HEADER_JIGSAW_RAINBOW__
#define __HEADER_JIGSAW_RAINBOW__

#include <stdint.h>

// Rainbow wash for grey pictures: a hue that runs along the diagonal (red at the top left,
// through yellow, green, cyan and blue, to purple at the bottom right; spread so each hue
// covers about the same area) is added to every pixel as a color shift with no brightness
// of its own, so light and dark stay where they were. Colors already there are kept (the
// shift adds to them). Near white and black there's no room for color, so those pixels move
// toward the middle just enough to take at least a minimum of it (white turns pastel, black
// a deep shade); light and dark pixels have their own minimum.

// Modes. Corners: blue (top left), pink (top right), green (bottom left), yellow (bottom
// right) blended across the picture; the plain blend fades toward the middle (the colors
// cancel there), FULL scales it back up.
#define RAINBOW_DIAGONAL 0
#define RAINBOW_CORNERS 1
#define RAINBOW_CORNERS_FULL 2
#define RAINBOW_PALETTES 6        // Corner color sets (rainbow.c); 0 = blue pink green yellow

#define RAINBOW_HUESPAN 300.0f   // Degrees of hue across the picture (not 360: the corners differ)

// rgb: w*h pixels, 3 bytes each, changed in place. strength: shift wanted (0..1, a fully
// saturated hue scaled so its biggest channel change is strength*255). minlight / mindark:
// smallest shift a light / dark pixel gets (<= strength; 0 = none, it stays as it is).
void rainbow_apply(uint8_t * rgb, uint32_t w, uint32_t h, int mode, int palette, float strength, float minlight, float mindark);

#endif

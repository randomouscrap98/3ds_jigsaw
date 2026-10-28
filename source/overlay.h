#ifndef __HEADER_JIGSAW_OVERLAY__
#define __HEADER_JIGSAW_OVERLAY__

#include <stdint.h>

// Picture overlays: a color treatment applied to the scaled picture before the pieces are
// drawn (pieces and the preview both get it; the cut doesn't change). Stored by number in
// saves and cache names, so only add to the end. Unknown numbers (a newer save) mean none.
#define OVERLAY_NONE 0
#define OVERLAY_HOLOGRAM 1  // Blue/pink/green/yellow corners blended, subtle on black (rainbow.h)
#define OVERLAY_RAINBOW 2   // Rainbow along the diagonal, red top left to purple bottom right (rainbow.h)
#define OVERLAYS 3
// Bump when an overlay's colors change, so caches baked with the old look are rebuilt
// (puzzlegfx keeps it in the cache header next to the overlay). 2: RAINBOW_EVEN.
#define OVERLAY_LOOK 2

// rgb: w*h pixels, 3 bytes each, changed in place
void overlay_apply(uint8_t overlay, uint8_t * rgb, uint32_t w, uint32_t h);

#endif

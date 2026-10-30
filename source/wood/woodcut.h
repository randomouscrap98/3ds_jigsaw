#ifndef __HEADER_JIGSAW_WOODCUT__
#define __HEADER_JIGSAW_WOODCUT__

#include "cutter.h"

#include <stdint.h>

// Shelved wooden cut experiments (not in the app: cutter_init doesn't know these styles).
// Numbered after the real styles so previews and tests can use one id range; if a real
// cut style is ever added, these move up.
#define CUT_STYLE_WOODEN (CUT_STYLES + 0)   // Random cut with waved edges + mixed knobs
#define CUT_STYLE_WOODEN2 (CUT_STYLES + 1)  // Stroke cut (strokecut) + snakes
#define CUT_STYLE_WOODEN3 (CUT_STYLES + 2)  // Cellular Potts cut (pottscut)
#define CUT_STYLE_WOODEN4 (CUT_STYLES + 3)  // Stroke cut + painted snakes (snakecut)
#define CUT_STYLE_WOODEN5 (CUT_STYLES + 4)  // Stroke cut + T-junction snakes (snakecut) + Wooden2 knobs
#define WOODCUT_STYLES_END (CUT_STYLES + 5)

// Like cutter_init, but also takes the wooden styles above (real styles go to cutter_init).
int woodcut_init(Cutter * c, uint8_t style, uint8_t whimsy, uint8_t set, uint16_t cols, uint16_t rows, uint32_t seed);

#endif

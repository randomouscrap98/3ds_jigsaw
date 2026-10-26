#ifndef __HEADER_JIGSAW_RENDER__
#define __HEADER_JIGSAW_RENDER__

#include <stdint.h>

#include "board.h"
#include "camera.h"
#include "puzzlegfx.h"

// If pieces turn out rotated the wrong way on hardware, flip this to -1
#define RENDER_ANGLE_SIGN 1

#define RENDER_COLOR_VOID   C2D_Color32(24, 26, 30, 255)
#define RENDER_COLOR_TABLE  C2D_Color32(46, 64, 58, 255)
#define RENDER_COLOR_SHADOW C2D_Color32(0, 0, 0, 110)

// Draw the table and every piece as seen through the camera. held is the group being
// dragged (drawn with a drop shadow) or -1.
void render_table(Board * b, Camera * c, PuzzleGfx * pg, int32_t held);
// Draw the preview picture centered in the given box, scaled down to fit if needed
void render_preview(PuzzleGfx * pg, float x, float y, float w, float h);

#endif

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
#define RENDER_COLOR_GLOW   C2D_Color32(255, 235, 170, 50)  // Alpha = how strong each blob is
#define RENDER_GLOW_SIZE    2.4f    // Glow blob diameter, in cells
#define RENDER_GLOW_TEX     64

// Drawing for the table. Owns the small textures it needs (the soft glow blob).
typedef struct {
  C3D_Tex glowtex;
  Tex3DS_SubTexture glowsub;
  uint8_t hasglow;
} Render;

int render_init(Render * r);
void render_free(Render * r);
// Draw the table and every piece as seen through the camera. held is the group being
// dragged (gets a soft glow on the table underneath everything) or -1.
void render_table(Render * r, Board * b, Camera * c, PuzzleGfx * pg, int32_t held);
// Draw the preview picture centered in the given box, scaled down to fit if needed
void render_preview(PuzzleGfx * pg, float x, float y, float w, float h);

#endif

#ifndef __HEADER_JIGSAW_RENDER__
#define __HEADER_JIGSAW_RENDER__

#include <stdint.h>

#include "board.h"
#include "camera.h"
#include "drawlist.h"
#include "puzzlegfx.h"

// If pieces turn out rotated the wrong way on hardware, flip this to -1
#define RENDER_ANGLE_SIGN 1

#define RENDER_COLOR_VOID   C2D_Color32(24, 26, 30, 255)
#define RENDER_TABLECOLORS  10      // Size of the table color palette (see render.c)
#define RENDER_COLOR_GLOW   C2D_Color32(0, 0, 0, 70)  // Shadow blob color; alpha = strength per blob
#define RENDER_GLOW_SIZE    1.52f   // Shadow blob diameter, in cells
#define RENDER_GLOW_CORE    0.5f    // Fraction of the blob radius that's solid before fading
#define RENDER_GLOW_SQUARE  4.0f    // Blob shape: 2 = circle, higher = squarer (rounded corners)
#define RENDER_GLOW_OFFSET  0.15f   // Shadow offset down-right, in cells (scales with zoom)
#define RENDER_GLOW_MAXDRAWN 1800  // No shadow when the last frame drew more pieces (pieces + blobs + text stay under citro2d's 4096 objects)
#define RENDER_GLOW_TEX     64
#define RENDER_COLOR_SELECT C2D_Color32(240, 200, 90, 255)  // Selected pieces tint (menu yellow)
#define RENDER_SELECT_BLEND 0.85f   // How much of the select color goes into a selected piece
#define RENDER_TRAY_SHADE   0.07f   // Tray color: the table color this far toward white (dark tables) or black
#define RENDER_TRAY_EDGESHADE 0.2f  // The same for the tray's top border
#define RENDER_TRAY_EDGE    5       // Top border height, pixels
#define RENDER_TRAY_DARK    110     // Tables with luma under this count as dark (the tray goes lighter)
#define RENDER_COLOR_BOX    C2D_Color32(240, 200, 90, 60)   // Selection box fill
#define RENDER_COLOR_BOXEDGE C2D_Color32(240, 200, 90, 230) // Selection box border

// Drawing for the table. Owns the small textures it needs (the soft glow blob).
typedef struct {
  C3D_Tex glowtex;
  Tex3DS_SubTexture glowsub;
  uint8_t hasglow;
  // Perf counters for the last render_table: piece sprites drawn, and how often the piece
  // texture changed between consecutive sprites (each change splits the GPU batch)
  uint32_t drawn, texswaps;
  const C3D_Tex * lasttex;
} Render;

// The tray: a strip from screen y top down to the bottom of the screen, its pieces (tray groups)
// drawn through cam (tray space -> screen)
typedef struct {
  float top;
  Camera cam;
} RenderTray;

int render_init(Render * r);
void render_free(Render * r);
// Draw the table and every piece as seen through the camera, pieces in drawlist order. held is the group being
// dragged (gets a soft offset shadow on the table, underneath everything) or -1. sel is
// one byte per group slot, nonzero = selected (tinted; if held is selected, the whole
// selection gets the shadow). table is an index into the table color palette (wraps). tray (NULL =
// none) is drawn over the table pieces and under the moving ones (the drawlist's last `moving`).
void render_table(Render * r, Board * b, const DrawList * dl, Camera * c, PuzzleGfx * pg,
    int32_t held, const uint8_t * sel, int table, const RenderTray * tray);
// Selection box between two world points, on top of everything
void render_box(Camera * c, float wx0, float wy0, float wx1, float wy1);
// Draw the preview picture centered in the given box, scaled down to fit if needed, at depth
// z with the given opacity (0..1)
void render_preview(PuzzleGfx * pg, float x, float y, float w, float h, float z, float alpha);

#endif

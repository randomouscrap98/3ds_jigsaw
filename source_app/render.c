#include "render.h"

#include <citro2d.h>
#include <math.h>

static void render_group(Board * b, Camera * c, PuzzleGfx * pg, uint16_t gid,
    float offset, const C2D_ImageTint * tint) {
  BoardGroup * g = b->groups + gid;
  float angle = RENDER_ANGLE_SIGN * g->rot * (float)M_PI_2;
  float zoom = c->zoom;
  float half = pg->layout.tile * zoom * 0.75f;  // Generous: rotated tile corner reach
  for(uint16_t p = g->first; p != BOARD_NONE; p = b->pieces[p].next) {
    float wx, wy, sx, sy;
    board_piece_center(b, p, &wx, &wy);
    camera_to_screen(c, wx, wy, &sx, &sy);
    sx += offset;
    sy += offset;
    if(sx < -half || sy < -half || sx > c->sw + half || sy > c->sh + half) continue;
    C2D_DrawImageAtRotated(puzzlegfx_piece(pg, p), sx, sy, 0.5f, angle, tint, zoom, zoom);
  }
}

void render_table(Board * b, Camera * c, PuzzleGfx * pg, int32_t held) {
  float x0, y0, x1, y1;
  camera_to_screen(c, 0, 0, &x0, &y0);
  camera_to_screen(c, b->width, b->height, &x1, &y1);
  C2D_DrawRectSolid(x0, y0, 0.1f, x1 - x0, y1 - y0, RENDER_COLOR_TABLE);
  for(uint32_t i = 0; i < b->ngroups; i++) {
    uint16_t gid = b->order[i];
    if((int32_t)gid == held) {
      C2D_ImageTint shadow;
      C2D_PlainImageTint(&shadow, RENDER_COLOR_SHADOW, 1.0f);
      render_group(b, c, pg, gid, 4, &shadow);
    }
    render_group(b, c, pg, gid, 0, NULL);
  }
}

void render_preview(PuzzleGfx * pg, float x, float y, float w, float h) {
  float pw = pg->previewsub.width, ph = pg->previewsub.height;
  float s = w / pw < h / ph ? w / pw : h / ph;
  if(s > 1) s = 1;
  C2D_DrawImageAt(puzzlegfx_preview(pg), x + (w - pw * s) / 2, y + (h - ph * s) / 2, 0.5f, NULL, s, s);
}

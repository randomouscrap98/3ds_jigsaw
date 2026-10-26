#include "render.h"
#include "piecebake.h"
#include "swizzle.h"

#include <citro2d.h>
#include <math.h>
#include <stdlib.h>

int render_init(Render * r) {
  r->hasglow = 0;
  if(!C3D_TexInit(&r->glowtex, RENDER_GLOW_TEX, RENDER_GLOW_TEX, GPU_RGBA8)) return 1;
  C3D_TexSetFilter(&r->glowtex, GPU_LINEAR, GPU_LINEAR);
  C3D_TexSetWrap(&r->glowtex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
  // White disc with a smooth falloff to fully transparent at the edge
  uint32_t * px = malloc(sizeof(uint32_t) * RENDER_GLOW_TEX * RENDER_GLOW_TEX);
  if(!px) {
    C3D_TexDelete(&r->glowtex);
    return 1;
  }
  const float half = RENDER_GLOW_TEX / 2.0f;
  for(int y = 0; y < RENDER_GLOW_TEX; y++) {
    for(int x = 0; x < RENDER_GLOW_TEX; x++) {
      float dx = (x + 0.5f - half) / half, dy = (y + 0.5f - half) / half;
      float d = sqrtf(dx * dx + dy * dy);
      float a = d >= 1 ? 0 : (1 - d) * (1 - d);
      px[y * RENDER_GLOW_TEX + x] = PIECEBAKE_RGBA(255, 255, 255, (uint8_t)(a * 255));
    }
  }
  swizzle_copy(r->glowtex.data, RENDER_GLOW_TEX, 0, 0, px, RENDER_GLOW_TEX, RENDER_GLOW_TEX, RENDER_GLOW_TEX);
  free(px);
  C3D_TexFlush(&r->glowtex);
  r->glowsub = (Tex3DS_SubTexture) {
    .width = RENDER_GLOW_TEX, .height = RENDER_GLOW_TEX,
    .left = 0, .top = 1, .right = 1, .bottom = 0,
  };
  r->hasglow = 1;
  return 0;
}

void render_free(Render * r) {
  if(r->hasglow) C3D_TexDelete(&r->glowtex);
  r->hasglow = 0;
}

// Soft blob under every piece of a group. Drawn before any pieces so it only ever
// shows on the bare table.
static void render_glow(Render * r, Board * b, Camera * c, uint16_t gid) {
  if(!r->hasglow) return;
  C2D_Image img = { .tex = &r->glowtex, .subtex = &r->glowsub };
  C2D_ImageTint tint;
  C2D_PlainImageTint(&tint, RENDER_COLOR_GLOW, 1.0f);
  float size = RENDER_GLOW_SIZE * b->cell * c->zoom;
  float scale = size / RENDER_GLOW_TEX, half = size / 2;
  for(uint16_t p = b->groups[gid].first; p != BOARD_NONE; p = b->pieces[p].next) {
    float wx, wy, sx, sy;
    board_piece_center(b, p, &wx, &wy);
    camera_to_screen(c, wx, wy, &sx, &sy);
    if(sx < -half || sy < -half || sx > c->sw + half || sy > c->sh + half) continue;
    C2D_DrawImageAtRotated(img, sx, sy, 0.3f, 0, &tint, scale, scale);
  }
}

static void render_group(Board * b, Camera * c, PuzzleGfx * pg, uint16_t gid) {
  BoardGroup * g = b->groups + gid;
  float angle = RENDER_ANGLE_SIGN * g->rot * (float)M_PI_2;
  float zoom = c->zoom;
  float half = pg->layout.tile * zoom * 0.75f;  // Generous: rotated tile corner reach
  for(uint16_t p = g->first; p != BOARD_NONE; p = b->pieces[p].next) {
    float wx, wy, sx, sy;
    board_piece_center(b, p, &wx, &wy);
    camera_to_screen(c, wx, wy, &sx, &sy);
    if(sx < -half || sy < -half || sx > c->sw + half || sy > c->sh + half) continue;
    C2D_DrawImageAtRotated(puzzlegfx_piece(pg, p), sx, sy, 0.5f, angle, NULL, zoom, zoom);
  }
}

void render_table(Render * r, Board * b, Camera * c, PuzzleGfx * pg, int32_t held) {
  float x0, y0, x1, y1;
  camera_to_screen(c, 0, 0, &x0, &y0);
  camera_to_screen(c, b->width, b->height, &x1, &y1);
  C2D_DrawRectSolid(x0, y0, 0.1f, x1 - x0, y1 - y0, RENDER_COLOR_TABLE);
  if(held >= 0) render_glow(r, b, c, held);
  for(uint32_t i = 0; i < b->ngroups; i++) render_group(b, c, pg, b->order[i]);
}

void render_preview(PuzzleGfx * pg, float x, float y, float w, float h) {
  float pw = pg->previewsub.width, ph = pg->previewsub.height;
  float s = w / pw < h / ph ? w / pw : h / ph;
  if(s > 1) s = 1;
  C2D_DrawImageAt(puzzlegfx_preview(pg), x + (w - pw * s) / 2, y + (h - ph * s) / 2, 0.5f, NULL, s, s);
}

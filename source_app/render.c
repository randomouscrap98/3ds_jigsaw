#include "render.h"
#include "swizzle.h"
#include "texutil.h"

#include <citro2d.h>
#include <math.h>
#include <stdlib.h>

// Same packing as C2D_Color32 (which is a function, so it can't be used in initializers)
#define RENDER_RGB(r, g, b) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | 0xFF000000u)

// Natural table colors: felt, wood, cloth. First one is the default.
static const u32 render_tablecolors[RENDER_TABLECOLORS] = {
  RENDER_RGB(46, 64, 58),     // Green felt
  RENDER_RGB(84, 112, 78),    // Moss
  RENDER_RGB(92, 66, 46),     // Walnut
  RENDER_RGB(178, 156, 122),  // Oak / tan
  RENDER_RGB(36, 52, 82),     // Navy
  RENDER_RGB(124, 148, 170),  // Slate blue
  RENDER_RGB(98, 38, 42),     // Burgundy
  RENDER_RGB(74, 56, 96),     // Plum
  RENDER_RGB(50, 50, 52),     // Charcoal
  RENDER_RGB(172, 172, 168),  // Light gray
};

int render_init(Render * r) {
  r->hasglow = 0;
  if(texutil_init(&r->glowtex, RENDER_GLOW_TEX, RENDER_GLOW_TEX, 1)) return 1;
  // White rounded square (superellipse): solid core, then a smooth falloff to fully
  // transparent at the edge
  uint32_t * px = malloc(sizeof(uint32_t) * RENDER_GLOW_TEX * RENDER_GLOW_TEX);
  if(!px) {
    C3D_TexDelete(&r->glowtex);
    return 1;
  }
  const float half = RENDER_GLOW_TEX / 2.0f;
  for(int y = 0; y < RENDER_GLOW_TEX; y++) {
    for(int x = 0; x < RENDER_GLOW_TEX; x++) {
      float dx = (x + 0.5f - half) / half, dy = (y + 0.5f - half) / half;
      float d = powf(powf(fabsf(dx), RENDER_GLOW_SQUARE) + powf(fabsf(dy), RENDER_GLOW_SQUARE),
          1.0f / RENDER_GLOW_SQUARE);
      float t = (d - RENDER_GLOW_CORE) / (1 - RENDER_GLOW_CORE);
      if(t < 0) t = 0;
      if(t > 1) t = 1;
      float a = 1 - t * t * (3 - 2 * t);  // smoothstep
      px[y * RENDER_GLOW_TEX + x] = SWIZZLE_RGBA(255, 255, 255, (uint8_t)(a * 255));
    }
  }
  swizzle_copy(r->glowtex.data, RENDER_GLOW_TEX, 0, 0, px, RENDER_GLOW_TEX, RENDER_GLOW_TEX, RENDER_GLOW_TEX);
  free(px);
  C3D_TexFlush(&r->glowtex);
  r->glowsub = texutil_subtex(0, 0, RENDER_GLOW_TEX, RENDER_GLOW_TEX, RENDER_GLOW_TEX, RENDER_GLOW_TEX);
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
  float off = RENDER_GLOW_OFFSET * b->cell * c->zoom;
  for(uint16_t p = b->groups[gid].first; p != BOARD_NONE; p = b->pieces[p].next) {
    float wx, wy, sx, sy;
    board_piece_center(b, p, &wx, &wy);
    camera_to_screen(c, wx, wy, &sx, &sy);
    sx += off;
    sy += off;
    if(sx < -half || sy < -half || sx > c->sw + half || sy > c->sh + half) continue;
    C2D_DrawImageAtRotated(img, sx, sy, 0.3f, 0, &tint, scale, scale);
  }
}

static void render_group(Board * b, Camera * c, PuzzleGfx * pg, uint16_t gid, const C2D_ImageTint * tint) {
  BoardGroup * g = b->groups + gid;
  float angle = RENDER_ANGLE_SIGN * g->rot * (float)M_PI_2;
  float zoom = c->zoom;
  float half = pg->layout.tile * zoom * 0.75f;  // Generous: rotated tile corner reach
  for(uint16_t p = g->first; p != BOARD_NONE; p = b->pieces[p].next) {
    float wx, wy, sx, sy;
    board_piece_center(b, p, &wx, &wy);
    camera_to_screen(c, wx, wy, &sx, &sy);
    if(sx < -half || sy < -half || sx > c->sw + half || sy > c->sh + half) continue;
    C2D_DrawImageAtRotated(puzzlegfx_piece(pg, p), sx, sy, 0.5f, angle, tint, zoom, zoom);
  }
}

void render_table(Render * r, Board * b, Camera * c, PuzzleGfx * pg, int32_t held,
    const uint8_t * sel, int table) {
  float x0, y0, x1, y1;
  camera_to_screen(c, 0, 0, &x0, &y0);
  camera_to_screen(c, b->width, b->height, &x1, &y1);
  C2D_DrawRectSolid(x0, y0, 0.1f, x1 - x0, y1 - y0, render_tablecolors[(unsigned)table % RENDER_TABLECOLORS]);
  if(held >= 0) {
    if(sel[held]) {
      for(uint32_t i = 0; i < b->ngroups; i++) {
        if(sel[b->order[i]]) render_glow(r, b, c, b->order[i]);
      }
    } else {
      render_glow(r, b, c, held);
    }
  }
  C2D_ImageTint seltint;
  C2D_PlainImageTint(&seltint, RENDER_COLOR_SELECT, RENDER_SELECT_BLEND);
  for(uint32_t i = 0; i < b->ngroups; i++) {
    uint16_t gid = b->order[i];
    render_group(b, c, pg, gid, sel[gid] ? &seltint : NULL);
  }
}

void render_box(Camera * c, float wx0, float wy0, float wx1, float wy1) {
  float x0, y0, x1, y1;
  camera_to_screen(c, wx0, wy0, &x0, &y0);
  camera_to_screen(c, wx1, wy1, &x1, &y1);
  if(x0 > x1) { float t = x0; x0 = x1; x1 = t; }
  if(y0 > y1) { float t = y0; y0 = y1; y1 = t; }
  float w = x1 - x0, h = y1 - y0;
  C2D_DrawRectSolid(x0, y0, 0.8f, w, h, RENDER_COLOR_BOX);
  C2D_DrawRectSolid(x0, y0, 0.8f, w, 1, RENDER_COLOR_BOXEDGE);
  C2D_DrawRectSolid(x0, y1 - 1, 0.8f, w, 1, RENDER_COLOR_BOXEDGE);
  C2D_DrawRectSolid(x0, y0, 0.8f, 1, h, RENDER_COLOR_BOXEDGE);
  C2D_DrawRectSolid(x1 - 1, y0, 0.8f, 1, h, RENDER_COLOR_BOXEDGE);
}

void render_preview(PuzzleGfx * pg, float x, float y, float w, float h) {
  float pw = pg->previewsub.width, ph = pg->previewsub.height;
  float s = w / pw < h / ph ? w / pw : h / ph;
  if(s > 1) s = 1;
  C2D_DrawImageAt(puzzlegfx_preview(pg), x + (w - pw * s) / 2, y + (h - ph * s) / 2, 0.5f, NULL, s, s);
}

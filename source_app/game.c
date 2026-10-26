#include "game.h"
#include "boardsave.h"
#include "rng.h"
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define GAME_ERR(...) { snprintf(err, errlen, __VA_ARGS__); }

static uint32_t game_cutseed(const char * name, uint32_t target, uint8_t style) {
  return rng_hashstr(name) ^ (target * 2654435761u) ^ (style * 0x9E3779B9u);
}

static int game_loadsave(Game * g, uint32_t * target, uint8_t * style, uint8_t * table) {
  char path[STORAGE_MAXPATH];
  storage_savepath(g->name, path);
  uint8_t * buf;
  size_t len;
  if(storage_readfile(path, &buf, &len)) return 1;
  int result = 1;
  if(len > 4) {
    uint32_t prefix = buf[0] | (buf[1] << 8) | (buf[2] << 16) | ((uint32_t)buf[3] << 24);
    *target = STORAGE_SAVE_TARGET(prefix);
    *style = STORAGE_SAVE_STYLE(prefix);
    *table = STORAGE_SAVE_TABLE(prefix);
    if(g->board.count) result = boardsave_read(&g->board, buf + 4, len - 4);
    else result = 0;  // Only wanted the target
  }
  free(buf);
  return result;
}

int game_init(Game * g, const char * name, uint32_t target, uint8_t style, SaveWriter * writer,
    progress_fn cb, void * udata,
    char * err, size_t errlen) {
  memset(g, 0, sizeof(Game));
  g->held = -1;
  g->writer = writer;
  snprintf(g->name, sizeof(g->name), "%s", name);
  int cont = target == 0;
  uint8_t table = 0;
  if(cont && game_loadsave(g, &target, &style, &table)) {
    GAME_ERR("Couldn't read the save file");
    return 1;
  }
  g->target = target;
  g->style = style;
  g->table = table;
  char ipath[STORAGE_MAXPATH], cpath[STORAGE_MAXPATH];
  storage_puzzlepath(name, ipath);
  storage_cachepath(name, target, style, cpath);
  if(puzzlegfx_load(&g->gfx, ipath, cpath, target, game_cutseed(name, target, style), style,
      cb, udata, err, errlen)) {
    return 1;
  }
  Layout * l = &g->gfx.layout;
  float tw = l->width * GAME_TABLESCALE, th = l->height * GAME_TABLESCALE;
  if(board_init(&g->board, l->cols, l->rows, l->cell, tw, th)) {
    GAME_ERR("Out of memory (board)");
    puzzlegfx_free(&g->gfx);
    return 1;
  }
  if(cont) {
    uint32_t t2;
    uint8_t s2, tb2;
    if(game_loadsave(g, &t2, &s2, &tb2)) {
      GAME_ERR("Save file doesn't match this puzzle; start a new one");
      game_free(g);
      return 1;
    }
  } else {
    float sw = tw * GAME_SCATTER, sh = th * GAME_SCATTER;
    board_scatter(&g->board, (uint32_t)osGetTime() ^ rng_hashstr(name), (tw - sw) / 2, (th - sh) / 2, sw, sh);
    g->dirty = 1;
  }
  camera_init(&g->cam, 320, 240, tw, th, GAME_MAXZOOM);
  stick_init(&g->stick, 156, GAME_STICK_DEADZONE, GAME_STICK_CURVE);
  stick_init(&g->cstick, GAME_CSTICK_RANGE, GAME_STICK_DEADZONE, GAME_STICK_CURVE);
  camera_set(&g->cam, tw / 2, th / 2, GAME_STARTCELL / l->cell);
  g->lastsave = osGetTime();
  if(!cont) game_save(g, 1);
  return 0;
}

void game_free(Game * g) {
  board_free(&g->board);
  puzzlegfx_free(&g->gfx);
  g->held = -1;
}

int game_save(Game * g, int background) {
  size_t len = 4 + boardsave_size(&g->board);
  uint8_t * buf = malloc(len);
  if(!buf) return 1;
  uint32_t prefix = STORAGE_SAVE_PREFIX(g->target, g->style, g->table);
  buf[0] = prefix & 0xFF;
  buf[1] = (prefix >> 8) & 0xFF;
  buf[2] = (prefix >> 16) & 0xFF;
  buf[3] = (prefix >> 24) & 0xFF;
  boardsave_write(&g->board, buf + 4);
  char path[STORAGE_MAXPATH];
  storage_savepath(g->name, path);
  int r = 0;
  if(background) {
    savewriter_submit(g->writer, path, buf, len);  // Takes buf
  } else {
    savewriter_flush(g->writer);
    r = storage_writefile(path, buf, len);
    free(buf);
  }
  if(r == 0) {
    g->dirty = 0;
    g->lastsave = osGetTime();
  }
  return r;
}

// Let go of whatever is held. Returns events.
static int game_release(Game * g) {
  int ev = 0;
  if(g->held >= 0) {
    int merges = board_drop(&g->board, g->held, g->board.cell * GAME_SNAP);
    if(merges) ev |= GAME_EV_MERGE;
    if(merges && board_complete(&g->board)) ev |= GAME_EV_COMPLETE;
    g->dirty = 1;
  }
  g->held = -1;
  g->panning = 0;
  return ev;
}

int game_update(Game * g, const GameInput * in) {
  int ev = 0;
  Camera * c = &g->cam;

  if(in->down & KEY_START) {
    ev |= game_release(g);
    game_save(g, 0);
    return ev | GAME_EV_EXIT;
  }
  // Hidden: SELECT cycles the table color (saved with the puzzle)
  if(in->down & KEY_SELECT) {
    g->table = (g->table + 1) % RENDER_TABLECOLORS;
    g->dirty = 1;
  }
  // Zoom: up/down, or X/B for the other hand
  if(in->repeat & (KEY_DUP | KEY_X)) camera_zoom(c, GAME_ZOOMSTEP);
  if(in->repeat & (KEY_DDOWN | KEY_B)) camera_zoom(c, 1.0f / GAME_ZOOMSTEP);
  // Smooth zoom on the New 3DS C-stick, up = in, same response curve as panning
  float zx, zy;
  if(stick_read(&g->cstick, 0, in->cstick.dy, &zx, &zy)) camera_zoom(c, powf(GAME_STICKZOOM, zy));
  // Pan: circle pad
  float px, py;
  if(stick_read(&g->stick, in->circle.dx, -in->circle.dy, &px, &py)) {
    camera_pan(c, px * GAME_PANSPEED, py * GAME_PANSPEED);
  }

  if(in->down & KEY_TOUCH) {
    float wx, wy;
    camera_to_world(c, in->touch.px, in->touch.py, &wx, &wy);
    int32_t gid = board_pick(&g->board, wx, wy);
    if(gid >= 0) {
      g->held = gid;
      board_raise(&g->board, gid);
    } else {
      g->panning = 1;
    }
    g->lastwx = wx;
    g->lastwy = wy;
    g->lastsx = in->touch.px;
    g->lastsy = in->touch.py;
  } else if(in->held & KEY_TOUCH) {
    float sx = in->touch.px, sy = in->touch.py;
    if(g->panning) {
      camera_pan(c, g->lastsx - sx, g->lastsy - sy);
    } else if(g->held >= 0) {
      // Dragging near the edge scrolls the table along
      float ex = 0, ey = 0;
      if(sx < GAME_EDGEPAN) ex = -GAME_EDGEPANSPEED;
      if(sx > c->sw - GAME_EDGEPAN) ex = GAME_EDGEPANSPEED;
      if(sy < GAME_EDGEPAN) ey = -GAME_EDGEPANSPEED;
      if(sy > c->sh - GAME_EDGEPAN) ey = GAME_EDGEPANSPEED;
      if(ex || ey) camera_pan(c, ex, ey);
    }
    g->lastsx = sx;
    g->lastsy = sy;
  } else if(in->up & KEY_TOUCH) {
    ev |= game_release(g);
  }

  if(g->held >= 0) {
    // Follow the stylus in world space (this also covers panning and zooming while held)
    float wx, wy;
    camera_to_world(c, g->lastsx, g->lastsy, &wx, &wy);
    if(wx < 0) wx = 0;
    if(wy < 0) wy = 0;
    if(wx > g->board.width) wx = g->board.width;
    if(wy > g->board.height) wy = g->board.height;
    board_move(&g->board, g->held, wx - g->lastwx, wy - g->lastwy);
    g->lastwx = wx;
    g->lastwy = wy;
    // Either shoulder turns clockwise, so both hands work the same
    if(in->down & (KEY_L | KEY_R)) board_rotate(&g->board, g->held, 1, wx, wy);
  }

  if(g->dirty && osGetTime() - g->lastsave > GAME_AUTOSAVE_MS) game_save(g, 1);
  return ev;
}

void game_render_bottom(Game * g, Render * r) {
  render_table(r, &g->board, &g->cam, &g->gfx, g->held, g->table);
}

void game_render_top(Game * g, TextDraw * td) {
  const u32 white = C2D_Color32(230, 230, 230, 255), dim = C2D_Color32(140, 140, 140, 255);
  render_preview(&g->gfx, 0, 0, 400, 204);
  if(board_complete(&g->board)) {
    textdraw_center(td, 200, 208, 0.7f, C2D_Color32(255, 220, 90, 255), "Complete!  START: menu");
  } else {
    textdraw_print(td, 6, 208, 0.5f, white, "%lu pieces, %lu groups left",
        (unsigned long)g->board.count, (unsigned long)g->board.ngroups);
    textdraw_print(td, 6, 224, 0.45f, dim, "L or R rotate held  Up/Down or X/B zoom  START save+menu");
  }
}

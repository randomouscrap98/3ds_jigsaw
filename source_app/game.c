#include "game.h"
#include "overlay.h"
#include "savefile.h"
#include "rng.h"
#include "whimsy.h"
#include "cutter.h"
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define GAME_ERR(...) do { snprintf(err, errlen, __VA_ARGS__); } while(0)

int game_init(Game * g, const char * name, uint32_t target, uint8_t style, uint8_t whimsy, uint8_t whimsyset, uint8_t overlay,
    uint8_t music, SaveWriter * writer,
    progress_fn cb, void * udata,
    char * err, size_t errlen) {
  memset(g, 0, sizeof(Game));
  g->held = -1;
  g->writer = writer;
  g->progress = cb;
  g->progressdata = udata;
  g->music = music;
  snprintf(g->name, sizeof(g->name), "%s", name);
  int cont = target == 0;
  // Continuing: read the save once. The header picks the cut; the board is restored
  // after the pieces exist.
  uint8_t * save = NULL;
  size_t savelen = 0;
  SaveHeader hdr;
  if(cont) {
    char spath[STORAGE_MAXPATH];
    storage_savepath(name, spath);
    int r = storage_readfile(spath, &save, &savelen) ? SAVEFILE_BAD : savefile_readheader(save, savelen, &hdr);
    if(r) {
      if(r == SAVEFILE_NEWER) GAME_ERR("Save is from a newer version of the app");
      else GAME_ERR("Couldn't read the save file");
      free(save);
      return 1;
    }
    target = hdr.target;
    style = hdr.style;
    whimsy = hdr.whimsy;
    whimsyset = hdr.whimsyset;
    overlay = hdr.overlay;
    g->table = hdr.table;
    g->music = hdr.music;
    g->volume = hdr.volume;
    g->playms = (u64)hdr.seconds * 1000;
    g->seed = savefile_seedfor(&hdr, name);
  } else {
    g->seed = savefile_cutseed(name, target, style);
  }
  // Unknown values (a newer save, or junk) mean none/cute, as the cutter treats them
  if(whimsy >= WHIMSY_LEVELS) whimsy = WHIMSY_NONE;
  if(whimsyset >= WHIMSY_SETS) whimsyset = WHIMSY_SET_CUTE;
  if(overlay >= OVERLAYS) overlay = OVERLAY_NONE;
  g->target = target;
  g->style = style;
  g->whimsy = whimsy;
  g->whimsyset = whimsyset;
  g->picoverlay = overlay;
  char ipath[STORAGE_MAXPATH], cpath[STORAGE_MAXPATH];
  storage_puzzlepath(name, ipath);
  storage_cachepath(name, target, style, whimsy, whimsyset, overlay, cpath);
  if(puzzlegfx_load(&g->gfx, &g->geo, ipath, cpath, target, g->seed, style, whimsy, whimsyset, overlay,
      cb, udata, err, errlen)) {
    free(save);
    return 1;
  }
  // Only one game per picture, so its caches for other counts/styles are dead weight
  storage_cleancaches(name, cpath);
  // Maze pieces are one thin cell wide: grabbed by a loose hull too, and they need more room
  int maze = style == CUT_STYLE_MAZE;
  if(maze && piecegeo_hulls(&g->geo)) {
    GAME_ERR("Out of memory (piece hulls)");
    piecegeo_free(&g->geo);
    puzzlegfx_free(&g->gfx);
    free(save);
    return 1;
  }
  Layout * l = &g->gfx.layout;
  float scale = maze ? GAME_TABLESCALE_MAZE : GAME_TABLESCALE;
  float tw = l->width * scale, th = l->height * scale;
  if(board_init(&g->board, &g->geo, tw, th)) {
    GAME_ERR("Out of memory (board)");
    piecegeo_free(&g->geo);
    puzzlegfx_free(&g->gfx);
    free(save);
    return 1;
  }
  g->sel = calloc(g->board.count, 1);
  g->boxsel = calloc(g->board.count, 1);
  if(!g->sel || !g->boxsel || drawlist_init(&g->dl, g->board.count)) {
    GAME_ERR("Out of memory (selection)");
    game_free(g);
    free(save);
    return 1;
  }
  if(cont) {
    // A save from before cut hashes (0) can't be checked; otherwise the cut must be the one
    // it was saved with (it could differ if the cache was rebuilt by a newer version)
    int changed = hdr.cuthash && hdr.cuthash != g->gfx.cuthash;
    int r = changed ? SAVEFILE_BAD : savefile_readboard(save, savelen, &hdr, &g->board);
    free(save);
    if(changed) {
      GAME_ERR("This puzzle's pieces changed since it was saved; start a new one");
      game_free(g);
      return 1;
    }
    if(r) {
      GAME_ERR("Save file doesn't match this puzzle; start a new one");
      game_free(g);
      return 1;
    }
  } else {
    float sw = tw * GAME_SCATTER, sh = th * GAME_SCATTER;
    uint32_t sseed = (uint32_t)osGetTime() ^ rng_hashstr(name);
    if(maze) board_scatterpack(&g->board, sseed, (tw - sw) / 2, (th - sh) / 2, sw, sh);
    else board_scatter(&g->board, sseed, (tw - sw) / 2, (th - sh) / 2, sw, sh);
    g->dirty = 1;
  }
  camera_init(&g->cam, GAME_SCREENW, GAME_SCREENH, tw, th, GAME_MAXZOOM);
  stick_init(&g->stick, GAME_CIRCLE_RANGE, GAME_STICK_DEADZONE, GAME_STICK_CURVE, GAME_PANMIN / GAME_PANSPEED);
  stick_init(&g->cstick, GAME_CSTICK_RANGE, GAME_STICK_DEADZONE, GAME_STICK_CURVE, GAME_PANMIN / GAME_PANSPEED);
  camera_set(&g->cam, tw / 2, th / 2, GAME_STARTCELL / l->cell);
  g->dldirty = 1;
  g->lastsave = osGetTime();
  g->lastframe = g->lastsave;
  if(!cont) game_save(g, 1);
  return 0;
}

void game_free(Game * g) {
  board_free(&g->board);
  piecegeo_free(&g->geo);
  puzzlegfx_free(&g->gfx);
  drawlist_free(&g->dl);
  free(g->sel);
  free(g->boxsel);
  g->sel = NULL;
  g->boxsel = NULL;
  g->nsel = 0;
  g->held = -1;
}

int game_save(Game * g, int background) {
  size_t len = savefile_size(&g->board);
  uint8_t * buf = malloc(len);
  if(!buf) return 1;
  SaveHeader sh;
  savefile_init(&sh);
  sh.appver[0] = APP_VERSION_MAJOR;
  sh.appver[1] = APP_VERSION_MINOR;
  sh.appver[2] = APP_VERSION_MICRO;
  sh.style = g->style;
  sh.whimsy = g->whimsy;
  sh.whimsyset = g->whimsyset;
  sh.overlay = g->picoverlay;
  sh.cuthash = g->gfx.cuthash;
  sh.seed = g->seed;
  sh.target = g->target;
  sh.table = g->table;
  sh.music = g->music;
  sh.volume = g->volume;
  sh.seconds = g->playms / 1000;
  savefile_write(&sh, &g->board, buf);
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
  if(!background) g->savefailed = r != 0;
  return r;
}

static void game_clearsel(Game * g) {
  if(g->nsel) memset(g->sel, 0, g->board.count);
  g->nsel = 0;
}

// Move every selected group by (dx, dy), cut short so all their piece centers stay on the
// table. (0, 0) just pushes them back on, e.g. after a rotation.
static void game_movesel(Game * g, float dx, float dy) {
  Board * b = &g->board;
  float x0, y0, x1, y1;
  if(!board_marked_bounds(b, g->sel, &x0, &y0, &x1, &y1)) return;
  if(x1 - x0 <= b->width) {
    if(x0 + dx < 0) dx = -x0;
    if(x1 + dx > b->width) dx = b->width - x1;
  }
  if(y1 - y0 <= b->height) {
    if(y0 + dy < 0) dy = -y0;
    if(y1 + dy > b->height) dy = b->height - y1;
  }
  if(dx == 0 && dy == 0) return;
  for(uint32_t i = 0; i < b->count; i++) {
    if(g->sel[i]) board_move(b, i, dx, dy);
  }
}

// Stylus lifted while boxing: a tap toggles the group under it, a drag replaces the
// selection with what the box covers
static void game_endbox(Game * g) {
  float wx, wy;
  camera_to_world(&g->cam, g->lastsx, g->lastsy, &wx, &wy);
  if(g->downmove < GAME_TAPDIST) {
    int32_t gid = board_pick(&g->board, wx, wy);
    if(gid >= 0) {
      g->sel[gid] = !g->sel[gid];
      if(g->sel[gid]) g->nsel++;
      else g->nsel--;
    }
  } else {
    game_clearsel(g);
    g->nsel = board_mark_rect(&g->board, g->boxwx, g->boxwy, wx, wy, g->sel);
  }
  g->boxing = 0;
}

// Let go of whatever is held. Returns events.
static int game_release(Game * g) {
  int ev = 0;
  if(g->boxing) game_endbox(g);
  if(g->movingsel) {
    // Moving a selection never snaps: it only goes where it's put
    g->dirty = 1;
  } else if(g->held >= 0) {
    // A quick tap turns the group a quarter, around the stylus, unless it would snap where
    // it is (snapping wins, so turns are deliberate)
    Board * b = &g->board;
    float snap = b->geo->cell * GAME_SNAP;
    if(g->downmove < GAME_TURNDIST && osGetTime() - g->downms < GAME_TURNMS &&
       b->groups[g->held].count <= GAME_TURNMAX && !board_can_merge(b, g->held, snap)) {
      board_rotate(b, g->held, 1, g->lastwx, g->lastwy);
    }
    int merges = board_drop(b, g->held, snap);
    if(merges) ev |= GAME_EV_MERGE;
    if(merges && board_complete(&g->board)) ev |= GAME_EV_COMPLETE;
    g->dirty = 1;
  }
  if(g->held >= 0) g->dldirty = 1;
  g->held = -1;
  g->ntop = 0;
  g->panning = 0;
  g->movingsel = 0;
  return ev;
}

// Squish the selected groups into a tidy block, pieces a little more than a piece apart, and
// keep them selected (on top) so the block can be dragged away
static void game_condense(Game * g) {
  Board * b = &g->board;
  if(board_condense_marked(b, g->sel, b->geo->cell * GAME_CONDENSE, g->style == CUT_STYLE_MAZE)) return;
  board_raise_marked(b, g->sel);
  g->dldirty = 1;
  g->dirty = 1;
}

int game_update(Game * g, const GameInput * gin) {
  int ev = 0;
  // While the modifier is held the buttons do only their modifier jobs; the sticks and
  // the stylus keep working as usual
  GameInput fin = *gin;
  const GameInput * in = &fin;
  if(gin->held & GAME_MODKEY) {
    if(gin->down & GAME_MOD_SKIPSONG) ev |= GAME_EV_SKIPSONG;
    if(gin->down & GAME_MOD_MUSICMODE) {
      // Saved with the next autosave, like the table color
      static const uint8_t next[] = {
        [SAVEFILE_MUSIC_OFF] = SAVEFILE_MUSIC_ORDER,
        [SAVEFILE_MUSIC_ORDER] = SAVEFILE_MUSIC_SHUFFLE,
        [SAVEFILE_MUSIC_SHUFFLE] = SAVEFILE_MUSIC_OFF,
      };
      g->music = next[g->music % 3];
      g->dirty = 1;
      ev |= GAME_EV_MUSICMODE;
    }
    if(gin->down & GAME_MOD_VOLUME) {
      static const uint8_t next[] = {
        [SAVEFILE_VOLUME_LOW] = SAVEFILE_VOLUME_MEDIUM,
        [SAVEFILE_VOLUME_MEDIUM] = SAVEFILE_VOLUME_HIGH,
        [SAVEFILE_VOLUME_HIGH] = SAVEFILE_VOLUME_LOW,
      };
      g->volume = next[g->volume % 3];
      g->dirty = 1;
      ev |= GAME_EV_VOLUME;
    }
    if((gin->down & GAME_MOD_CONDENSE) && g->nsel && g->held < 0 && !g->boxing) game_condense(g);
    const u32 keep = KEY_TOUCH | GAME_MODKEY | GAME_MOD_KEEPKEYS;
    fin.down &= keep;
    fin.held &= keep;
    fin.repeat &= keep;
  }
  Camera * c = &g->cam;
  u64 now = osGetTime();
  u64 dt = now - g->lastframe;
  if(dt > GAME_MAXFRAME_MS) dt = GAME_MAXFRAME_MS;
  g->lastframe = now;
  if(!board_complete(&g->board)) g->playms += dt;
  // Info overlay fades in while Y is held, out when let go
  g->overlay += (in->held & GAME_MODKEY ? 1.0f : -1.0f) * dt / GAME_OVERLAY_FADE_MS;
  if(g->overlay < 0) g->overlay = 0;
  if(g->overlay > 1) g->overlay = 1;

  if(in->down & KEY_START) {
    ev |= game_release(g);
    // The SD can take seconds (waiting on an autosave in flight too): don't look frozen
    if(g->progress) g->progress(g->progressdata, "Saving...", 0, 1);
    // Leaving after a failed save would lose the progress, so stay and say so
    if(game_save(g, 0) == 0) return ev | GAME_EV_EXIT;
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
    if(in->held & GAME_SELECTKEYS) {
      g->boxing = 1;
      g->boxwx = wx;
      g->boxwy = wy;
    } else if(gid >= 0 && g->sel[gid]) {
      g->held = gid;
      g->movingsel = 1;
      board_raise_marked(&g->board, g->sel);
      g->ntop = g->nsel;
      g->dldirty = 1;
    } else {
      // Touching anything outside the selection drops it, then acts as usual
      game_clearsel(g);
      if(gid >= 0) {
        g->held = gid;
        board_raise(&g->board, gid);
        g->ntop = 1;
        g->dldirty = 1;
      } else {
        g->panning = 1;
      }
    }
    g->lastwx = wx;
    g->lastwy = wy;
    g->lastsx = in->touch.px;
    g->lastsy = in->touch.py;
    g->downsx = in->touch.px;
    g->downsy = in->touch.py;
    g->downmove = 0;
    g->downms = now;
  } else if(in->held & KEY_TOUCH) {
    float sx = in->touch.px, sy = in->touch.py;
    float d = hypotf(sx - g->downsx, sy - g->downsy);
    if(d > g->downmove) g->downmove = d;
    if(g->panning) {
      camera_pan(c, g->lastsx - sx, g->lastsy - sy);
    } else if(g->held >= 0 || (g->boxing && g->downmove >= GAME_TAPDIST)) {
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
    if(g->movingsel) game_movesel(g, wx - g->lastwx, wy - g->lastwy);
    else board_move(&g->board, g->held, wx - g->lastwx, wy - g->lastwy);
    g->lastwx = wx;
    g->lastwy = wy;
    // Either shoulder turns clockwise, so both hands work the same. A selection turns as
    // one cluster around the stylus.
    if(in->down & (KEY_L | KEY_R)) {
      if(g->movingsel) {
        for(uint32_t i = 0; i < g->board.count; i++) {
          if(g->sel[i]) board_rotate(&g->board, i, 1, wx, wy);
        }
        game_movesel(g, 0, 0);
        g->dldirty = 1;   // Selected groups moved relative to each other
      } else {
        board_rotate(&g->board, g->held, 1, wx, wy);
      }
    }
  }

  if(g->dldirty) {
    u64 t = svcGetSystemTick();
    Layout * l = &g->gfx.layout;
    drawlist_build(&g->dl, &g->board, (l->maxw > l->maxh ? l->maxw : l->maxh) / 2.0f, g->gfx.pageof, l->pages, g->ntop);
    g->perf.build = (svcGetSystemTick() - t) / (SYSCLOCK_ARM11 / 1000.0f);
    g->perf.layers = g->dl.layers;
    g->dldirty = 0;
  }
  if(g->dirty && osGetTime() - g->lastsave > GAME_AUTOSAVE_MS) game_save(g, 1);
  return ev;
}

void game_render_bottom(Game * g, Render * r) {
  if(g->boxing && g->downmove >= GAME_TAPDIST) {
    // Show what the box would select while it's being drawn (it replaces the selection)
    float wx, wy;
    camera_to_world(&g->cam, g->lastsx, g->lastsy, &wx, &wy);
    memset(g->boxsel, 0, g->board.count);
    board_mark_rect(&g->board, g->boxwx, g->boxwy, wx, wy, g->boxsel);
    render_table(r, &g->board, &g->dl, &g->cam, &g->gfx, g->held, g->boxsel, g->table);
    render_box(&g->cam, g->boxwx, g->boxwy, wx, wy);
  } else {
    render_table(r, &g->board, &g->dl, &g->cam, &g->gfx, g->held, g->sel, g->table);
  }
}

#define GAME_ICONZ 0.9f

// Music mode icon, about 10 px square around (cx, cy): an X for off, a right arrow for in
// order, a die showing three for shuffled. bg = die pip color.
static void game_drawmode(uint8_t mode, float cx, float cy, u32 c, u32 bg) {
  if(mode == SAVEFILE_MUSIC_OFF) {
    C2D_DrawLine(cx - 4, cy - 4, c, cx + 4, cy + 4, c, 2, GAME_ICONZ);
    C2D_DrawLine(cx - 4, cy + 4, c, cx + 4, cy - 4, c, 2, GAME_ICONZ);
  } else if(mode == SAVEFILE_MUSIC_SHUFFLE) {
    C2D_DrawRectSolid(cx - 5, cy - 5, GAME_ICONZ, 10, 10, c);
    for(int i = -1; i <= 1; i++) C2D_DrawCircleSolid(cx + 2.8f * i, cy - 2.8f * i, GAME_ICONZ, 1.3f, bg);
  } else {
    C2D_DrawRectSolid(cx - 5, cy - 1.5f, GAME_ICONZ, 6, 3, c);
    C2D_DrawTriangle(cx + 1, cy - 4.5f, c, cx + 1, cy + 4.5f, c, cx + 5.5f, cy, c, GAME_ICONZ);
  }
}

// Speaker from x, centered on cy, with no / one / two sound waves for low / medium / high
static void game_drawvolume(uint8_t volume, float x, float cy, u32 c) {
  C2D_DrawRectSolid(x, cy - 2, GAME_ICONZ, 3, 4, c);
  C2D_DrawTriangle(x + 3, cy - 2, c, x + 7, cy - 5, c, x + 7, cy + 5, c, GAME_ICONZ);
  C2D_DrawTriangle(x + 3, cy - 2, c, x + 7, cy + 5, c, x + 3, cy + 2, c, GAME_ICONZ);
  int waves = volume == SAVEFILE_VOLUME_HIGH ? 2 : volume == SAVEFILE_VOLUME_MEDIUM ? 1 : 0;
  for(int w = 0; w < waves; w++) {
    float rad = 4 + 3.5f * w;
    const int segs = 6;
    const float span = 1.0f;  // Radians each side of horizontal
    for(int i = 0; i < segs; i++) {
      float a0 = -span + 2 * span * i / segs, a1 = -span + 2 * span * (i + 1) / segs;
      C2D_DrawLine(x + 7 + rad * cosf(a0), cy + rad * sinf(a0), c,
          x + 7 + rad * cosf(a1), cy + rad * sinf(a1), c, 1.5f, GAME_ICONZ);
    }
  }
}

void game_render_top(Game * g, TextDraw * td) {
  const u32 white = C2D_Color32(230, 230, 230, 255), dim = C2D_Color32(140, 140, 140, 255);
  render_preview(&g->gfx, 0, 0, 400, 204);
  const u32 warn = C2D_Color32(255, 110, 100, 255);
  if(g->savefailed) {
    C2D_DrawRectSolid(0, 0, 400, 20, 0.7f, C2D_Color32(0, 0, 0, 190));
    textdraw_center(td, 200, 3, 0.5f, warn, "Couldn't save! Check the SD card, then press START again");
  } else if(savewriter_failed(g->writer)) {
    C2D_DrawRectSolid(0, 0, 400, 20, 0.7f, C2D_Color32(0, 0, 0, 190));
    textdraw_center(td, 200, 3, 0.5f, warn, "Autosave failed! Check the SD card");
  }
  if(board_complete(&g->board)) {
    textdraw_center(td, 200, 208, 0.7f, C2D_Color32(255, 220, 90, 255), "Complete!  START: menu");
  } else {
    if(g->nsel) {
      textdraw_print(td, 6, 208, 0.5f, white, "%lu pieces, %lu groups left, %lu selected",
          (unsigned long)g->board.count, (unsigned long)g->board.ngroups, (unsigned long)g->nsel);
    } else {
      textdraw_print(td, 6, 208, 0.5f, white, "%lu pieces, %lu groups left",
          (unsigned long)g->board.count, (unsigned long)g->board.ngroups);
    }
    textdraw_print(td, 6, 224, 0.45f, dim, "L or R rotate held  Up/Down or X/B zoom  START save+menu");
  }
  if(g->overlay > 0) {
    // Info panel over the middle of the preview (hold Y)
    u8 a = (u8)(g->overlay * 255);
    char time[16];
    textdraw_fmttime(time, sizeof(time), g->playms / 1000);
    C2D_DrawRectSolid(70, 66, 0.7f, 260, 98, C2D_Color32(16, 16, 20, (u8)(g->overlay * 210)));
    textdraw_center(td, 200, 74, 0.8f, C2D_Color32(255, 220, 90, a), "%s", time);
    textdraw_center(td, 200, 108, 0.5f, C2D_Color32(230, 230, 230, a), "%lu pieces, %lu groups left",
        (unsigned long)g->board.count, (unsigned long)g->board.ngroups);
    // CPU use (main thread, then music decoding) and the song
    const GamePerf * pf = &g->perf;
    u32 pc = C2D_Color32(170, 170, 170, a);
    int pct = pf->frame > 0 ? (int)(100 * pf->cpu / pf->frame) : 0;
    int mpct = pf->frame > 0 ? (int)(100 * (pf->music + pf->musicsd) / pf->frame) : 0;
    textdraw_center(td, 200, 128, 0.45f, pc, "CPU %.1f ms (%d%%)  music %.1f+%.1fms (%d%%)",
        pf->cpu, pct, pf->music, pf->musicsd, mpct);
    // Song name rolls if it's too long, starting over each time the song changes
    if(strcmp(g->lastsong, pf->song)) {
      snprintf(g->lastsong, sizeof(g->lastsong), "%s", pf->song);
      g->songstart = osGetTime();
    }
    // Volume and mode icons stay put; the rest of the line is the song
    game_drawvolume(g->volume, 80, 151, pc);
    game_drawmode(g->music, 106, 151, pc, C2D_Color32(16, 16, 20, a));
    const char * song = g->music == SAVEFILE_MUSIC_OFF ? "Music off" : pf->song[0] ? pf->song : "No music";
    textdraw_marquee(td, 118, 144, 202, 0.45f, pc,
        (osGetTime() - g->songstart) * GAME_MARQUEE_SPEED / 1000, song);
  }
}

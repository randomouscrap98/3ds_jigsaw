#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defaults.h"
#include "game.h"
#include "puzzlemenu.h"
#include "savewriter.h"
#include "render.h"
#include "sfx.h"
#include "storage.h"
#include "textdraw.h"

// Piece pages + preview need ~9MB of linear memory; the rest is better spent on the
// regular heap (image decoding). This is a libctru override hook.
u32 __ctru_linear_heap_size = 20 * 1024 * 1024;

#define MAIN_BG_TOP    C2D_Color32(30, 32, 38, 255)
#define MAIN_BG_BOTTOM C2D_Color32(24, 26, 30, 255)
#define MAIN_REPEAT_DELAY 20
#define MAIN_REPEAT_INTERVAL 5

typedef enum { MODE_MENU, MODE_GAME } MainMode;

// Everything the app owns, so hooks and callbacks can reach it
typedef struct {
  C3D_RenderTarget * top;
  C3D_RenderTarget * bottom;
  TextDraw td;
  Render render;
  SaveWriter writer;
  PuzzleMenu menu;
  Game game;
  Sfx sfx;
  MainMode mode;
  aptHookCookie hook;
} App;

static void app_frame_begin(App * app) {
  C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
  textdraw_begin(&app->td);
  C2D_TargetClear(app->top, MAIN_BG_TOP);
  C2D_TargetClear(app->bottom, MAIN_BG_BOTTOM);
}

// Called while cutting a puzzle, which blocks for a while; draws a progress screen
static void app_progress(void * udata, const char * msg, int done, int total) {
  App * app = udata;
  app_frame_begin(app);
  C2D_SceneBegin(app->top);
  textdraw_center(&app->td, 200, 100, 0.7f, C2D_Color32(230, 230, 230, 255), "%s", msg);
  if(total > 1) {
    C2D_DrawRectSolid(80, 136, 0.5f, 240, 10, C2D_Color32(60, 60, 60, 255));
    C2D_DrawRectSolid(80, 136, 0.6f, 240.0f * done / total, 10, C2D_Color32(240, 200, 90, 255));
    textdraw_center(&app->td, 200, 152, 0.5f, C2D_Color32(160, 160, 160, 255), "%d%%", (int)(100.0f * done / total));
  }
  C2D_SceneBegin(app->bottom);
  C3D_FrameEnd(0);
}

static void app_hook(APT_HookType type, void * param) {
  App * app = param;
  if(app->mode != MODE_GAME) return;
  if(type == APTHOOK_ONSUSPEND || type == APTHOOK_ONSLEEP || type == APTHOOK_ONEXIT) {
    // Synchronous: it must be on the SD before we might be killed
    if(app->game.dirty) game_save(&app->game, 0);
    savewriter_flush(&app->writer);
  }
}

static void app_start_game(App * app, PuzzleChoice choice) {
  char err[256] = "";
  if(game_init(&app->game, choice.name, choice.target, choice.style, &app->writer,
      app_progress, app, err, sizeof(err))) {
    puzzlemenu_setstatus(&app->menu, err[0] ? err : "Couldn't start the puzzle");
    return;
  }
  app->mode = MODE_GAME;
}

static void app_end_game(App * app) {
  game_free(&app->game);
  app->mode = MODE_MENU;
  savewriter_flush(&app->writer);  // The menu reads saves back
  puzzlemenu_refresh(&app->menu);
}

int main(int argc, char ** argv) {
  static App app;
  memset(&app, 0, sizeof(app));
  gfxInitDefault();
  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
  C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
  C2D_Prepare();
  app.top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  app.bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
  hidSetRepeatParameters(MAIN_REPEAT_DELAY, MAIN_REPEAT_INTERVAL);
  textdraw_init(&app.td);
  render_init(&app.render);  // Without it there's just no shadow
  sfx_init(&app.sfx);
  storage_init();
  savewriter_init(&app.writer);  // On failure, saves are just written inline
  if(R_SUCCEEDED(romfsInit())) {
    // Failure isn't fatal: the menu just shows whatever made it
    defaults_install(app_progress, &app);
    romfsExit();
  }
  puzzlemenu_init(&app.menu);
  app.mode = MODE_MENU;
  aptHook(&app.hook, app_hook, &app);

  while(aptMainLoop()) {
    hidScanInput();
    GameInput in;
    in.down = hidKeysDown();
    in.held = hidKeysHeld();
    in.up = hidKeysUp();
    in.repeat = hidKeysDownRepeat();
    hidTouchRead(&in.touch);
    hidCircleRead(&in.circle);
    hidCstickRead(&in.cstick);

    if(app.mode == MODE_MENU) {
      PuzzleChoice choice = puzzlemenu_update(&app.menu, in.down, in.repeat);
      if(choice.quit) break;
      if(choice.name) app_start_game(&app, choice);
    } else {
      int ev = game_update(&app.game, &in);
      if(ev & GAME_EV_COMPLETE) sfx_play(&app.sfx, SFX_COMPLETE);
      else if(ev & GAME_EV_MERGE) sfx_play(&app.sfx, SFX_MERGE);
      if(ev & GAME_EV_EXIT) app_end_game(&app);
    }

    app_frame_begin(&app);
    C2D_SceneBegin(app.top);
    if(app.mode == MODE_MENU) puzzlemenu_render_top(&app.menu, &app.td);
    else game_render_top(&app.game, &app.td);
    C2D_SceneBegin(app.bottom);
    if(app.mode == MODE_MENU) puzzlemenu_render_bottom(&app.menu, &app.td);
    else game_render_bottom(&app.game, &app.render);
    C3D_FrameEnd(0);
  }

  if(app.mode == MODE_GAME) {
    game_save(&app.game, 0);
    game_free(&app.game);
  }
  aptUnhook(&app.hook);
  savewriter_free(&app.writer);
  puzzlemenu_free(&app.menu);
  sfx_free(&app.sfx);
  render_free(&app.render);
  textdraw_free(&app.td);
  C2D_Fini();
  C3D_Fini();
  gfxExit();
  return 0;
}

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defaults.h"
#include "game.h"
#include "music.h"
#include "picfiles.h"
#include "puzzlemenu.h"
#include "savewriter.h"
#include "render.h"
#include "sfx.h"
#include "storage.h"
#include "textdraw.h"
#include "upload.h"

#define MAIN_BG_TOP    C2D_Color32(30, 32, 38, 255)
#define MAIN_BG_BOTTOM C2D_Color32(24, 26, 30, 255)
#define MAIN_REPEAT_DELAY 20
#define MAIN_REPEAT_INTERVAL 5
#define MAIN_CHIME_DELAY 4      // Frames between the last piece's click and the completion chime

#define MAIN_PERF_SMOOTH 0.1f  // Weight of the newest frame in the perf averages

static float main_ms(u64 ticks) {
  return ticks / (SYSCLOCK_ARM11 / 1000.0f);
}

static void main_smooth(float * avg, float v) {
  *avg += (v - *avg) * MAIN_PERF_SMOOTH;
}

typedef enum { MODE_MENU, MODE_GAME, MODE_UPLOAD } MainMode;

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
  Music music;
  Upload upload;
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
  if(app->mode == MODE_UPLOAD) {
    // The network doesn't survive sleep or HOME: stop it, start again on the way back
    if(type == APTHOOK_ONSUSPEND || type == APTHOOK_ONSLEEP) upload_pause(&app->upload);
    if(type == APTHOOK_ONRESTORE || type == APTHOOK_ONWAKEUP) upload_resume(&app->upload);
    return;
  }
  if(app->mode != MODE_GAME) return;
  if(type == APTHOOK_ONSUSPEND || type == APTHOOK_ONSLEEP || type == APTHOOK_ONEXIT) {
    // Synchronous: it must be on the SD before we might be killed
    if(app->game.dirty) game_save(&app->game, 0);
    savewriter_flush(&app->writer);
  }
}

// Music follows the game's mode
static void app_music(App * app) {
  uint8_t m = app->game.music;
  music_setmode(&app->music, m == SAVEFILE_MUSIC_OFF ? MUSIC_OFF : m == SAVEFILE_MUSIC_SHUFFLE ? MUSIC_SHUFFLE : MUSIC_ORDER);
}

static void app_start_game(App * app, PuzzleChoice choice) {
  char err[256] = "";
  app_progress(app, "Loading", 0, 1);
  puzzlemenu_waitidle(&app->menu);
  if(game_init(&app->game, choice.name, choice.target, choice.style, choice.whimsy, choice.whimsyset, choice.overlay,
      choice.music, &app->writer, app_progress, app, err, sizeof(err))) {
    puzzlemenu_setstatus(&app->menu, err[0] ? err : "Couldn't start the puzzle");
    return;
  }
  app->mode = MODE_GAME;
  app_music(app);
}

// Rename a picture (and its save and caches) to what's typed on the system keyboard
static void app_rename(App * app, const char * name) {
  char old[PMENU_MAXNAME], typed[STORAGE_MAXBASE * 4 + 1], base[STORAGE_MAXBASE + 1], newname[PMENU_MAXNAME];
  snprintf(old, sizeof(old), "%s", name);   // name is in the menu's list, which refresh frees
  app_progress(app, "Opening the keyboard...", 0, 1);  // Takes a few seconds
  puzzlemenu_waitidle(&app->menu);          // The thumbnail thread may be reading the picture
  savewriter_flush(&app->writer);
  const char * dot = strrchr(old, '.');
  snprintf(base, sizeof(base), "%.*s", dot ? (int)(dot - old) : (int)strlen(old), old);
  SwkbdState kb;
  swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, STORAGE_MAXBASE);
  swkbdSetInitialText(&kb, base);
  swkbdSetHintText(&kb, "New name for the picture");
  swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
  if(swkbdInputText(&kb, typed, sizeof(typed)) != SWKBD_BUTTON_CONFIRM) return;
  storage_cleanbase(typed, base, sizeof(base));
  app_progress(app, "Renaming...", 0, 1);
  PicDirs dirs = { STORAGE_PUZZLES, STORAGE_SAVES, STORAGE_CACHE };
  int r = picfiles_rename(&dirs, old, base, newname, sizeof(newname));
  puzzlemenu_refresh(&app->menu);
  puzzlemenu_select(&app->menu, r == PICFILES_OK ? newname : old);
  if(r == PICFILES_TAKEN) puzzlemenu_setstatus(&app->menu, "There's already a picture with that name");
  else if(r) puzzlemenu_setstatus(&app->menu, "Couldn't rename the picture");
}

// Delete a picture with its save and caches (the menu already asked)
static void app_delete(App * app, const char * name) {
  char old[PMENU_MAXNAME];
  snprintf(old, sizeof(old), "%s", name);   // name is in the menu's list, which refresh frees
  puzzlemenu_waitidle(&app->menu);          // The thumbnail thread may be reading the picture
  savewriter_flush(&app->writer);
  PicDirs dirs = { STORAGE_PUZZLES, STORAGE_SAVES, STORAGE_CACHE };
  int r = picfiles_delete(&dirs, old);
  puzzlemenu_refresh(&app->menu);           // The cursor stays put: on the next puzzle
  if(r) puzzlemenu_setstatus(&app->menu, "Couldn't delete the picture");
}

static void app_end_game(App * app) {
  music_setmode(&app->music, MUSIC_OFF);  // Fades out; the thread frees the decoder within a DSP frame (~5 ms)
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
  music_init(&app.music, app.sfx.ok);  // Silently nothing on failure
  app.mode = MODE_MENU;
  aptHook(&app.hook, app_hook, &app);

  u64 lastloop = svcGetSystemTick();
  while(aptMainLoop()) {
    u64 t0 = svcGetSystemTick();
    GamePerf * pf = &app.game.perf;
    main_smooth(&pf->frame, main_ms(t0 - lastloop));
    lastloop = t0;
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
      if(choice.upload) {
        upload_open(&app.upload);
        app.mode = MODE_UPLOAD;
      }
      if(choice.name && choice.rename) app_rename(&app, choice.name);
      else if(choice.name && choice.del) app_delete(&app, choice.name);
      else if(choice.name) app_start_game(&app, choice);
    } else if(app.mode == MODE_UPLOAD) {
      if(upload_update(&app.upload, in.down)) {
        upload_close(&app.upload);
        app.mode = MODE_MENU;
        puzzlemenu_refresh(&app.menu);   // New pictures show up
      }
    } else {
      u64 tu = svcGetSystemTick();
      int ev = game_update(&app.game, &in);
      main_smooth(&pf->update, main_ms(svcGetSystemTick() - tu));
      if(ev & GAME_EV_MERGE) sfx_play(&app.sfx, SFX_MERGE);
      if(ev & GAME_EV_COMPLETE) sfx_play_later(&app.sfx, SFX_COMPLETE, MAIN_CHIME_DELAY);
      if(ev & GAME_EV_SKIPSONG) music_skip(&app.music);
      if(ev & GAME_EV_MUSICMODE) app_music(&app);
      if(ev & GAME_EV_EXIT) app_end_game(&app);
    }

    sfx_tick(&app.sfx);
    u64 t1 = svcGetSystemTick();
    app_frame_begin(&app);
    u64 t2 = svcGetSystemTick();
    C2D_SceneBegin(app.top);
    if(app.mode == MODE_MENU) puzzlemenu_render_top(&app.menu, &app.td);
    else if(app.mode == MODE_UPLOAD) upload_render_top(&app.upload, &app.td);
    else game_render_top(&app.game, &app.td);
    C2D_SceneBegin(app.bottom);
    if(app.mode == MODE_MENU) puzzlemenu_render_bottom(&app.menu, &app.td);
    else if(app.mode == MODE_UPLOAD) upload_render_bottom(&app.upload, &app.td);
    else {
      u64 tb = svcGetSystemTick();
      game_render_bottom(&app.game, &app.render);
      main_smooth(&pf->bottom, main_ms(svcGetSystemTick() - tb));
      pf->drawn = app.render.drawn;
      pf->texswaps = app.render.texswaps;
    }
    u64 t3 = svcGetSystemTick();
    C3D_FrameEnd(0);
    main_smooth(&pf->cpu, main_ms((t1 - t0) + (t3 - t2)));
    u64 mdec, msd;
    music_busy(&app.music, &mdec, &msd);
    main_smooth(&pf->music, main_ms(mdec));
    main_smooth(&pf->musicsd, main_ms(msd));
    music_name(&app.music, pf->song, sizeof(pf->song));
  }

  if(app.mode == MODE_GAME) {
    game_save(&app.game, 0);
    game_free(&app.game);
  }
  if(app.mode == MODE_UPLOAD) upload_close(&app.upload);
  aptUnhook(&app.hook);
  savewriter_free(&app.writer);
  puzzlemenu_free(&app.menu);
  music_free(&app.music);  // Before sfx_free shuts ndsp down
  sfx_free(&app.sfx);
  render_free(&app.render);
  textdraw_free(&app.td);
  C2D_Fini();
  C3D_Fini();
  gfxExit();
  return 0;
}

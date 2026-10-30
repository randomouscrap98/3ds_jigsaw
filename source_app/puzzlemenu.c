#include "puzzlemenu.h"
#include "cutter.h"
#include "overlay.h"

#include <citro2d.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PMENU_HELP_MOVE "Up/Down: move   A: select   B: back"

static const uint32_t pmenu_counts[PMENU_NUMCOUNTS] = { 50, 100, 250, 500 };  // Targets; the grid lands near them
static const char * const pmenu_styles[CUT_STYLES] = { "Classic", "Irregular", "Wild", "Random", "Random Rect", "Maze" };  // CUT_STYLE_*
static const char * const pmenu_whimsies[WHIMSY_LEVELS] = { "", ", few whimsies", ", many whimsies" };  // WHIMSY_*
static const char * const pmenu_overlays[OVERLAYS] = { "", ", hologram", ", rainbow" };  // OVERLAY_*

static int puzzlemenu_isimage(const char * name) {
  const char * dot = strrchr(name, '.');
  if(!dot) return 0;
  return !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg") ||
         !strcasecmp(dot, ".png") || !strcasecmp(dot, ".bmp");
}

// Length of a file name without its extension (what the menu shows)
static int puzzlemenu_namelen(const char * name) {
  const char * dot = strrchr(name, '.');
  return dot ? (int)(dot - name) : (int)strlen(name);
}

static int puzzlemenu_cmp(const void * a, const void * b) {
  return strcasecmp(((const PuzzleEntry *)a)->name, ((const PuzzleEntry *)b)->name);
}

// Confirm menu only appears when there's a save to throw away. Returning NULL makes
// littlemenu treat the item as a plain click.
static tui_menu * puzzlemenu_create_confirm(tui_menu_item_data * data, tui_menu * parent, tui_menu_unit_t pos) {
  PuzzleMenu * pm = data->ptr;
  int sel = pm->root.current - PMENU_FIRST;
  if(sel < 0 || sel >= pm->nentries || !pm->entries[sel].hassave) return NULL;
  // Say what's about to start, since the settings aren't on the line that was pressed
  snprintf(pm->confirm.items[1].name, sizeof(pm->confirm.items[1].name), "Yes, erase it: new %lu pieces, %s%s%s",
      (unsigned long)pmenu_counts[(unsigned)pm->pieces % PMENU_NUMCOUNTS], pmenu_styles[(unsigned)pm->style % CUT_STYLES],
      pmenu_whimsies[(unsigned)pm->whimsy % WHIMSY_LEVELS], pmenu_overlays[(unsigned)pm->overlay % OVERLAYS]);
  tui_menu_reset_ui(&pm->confirm);
  return &pm->confirm;
}

// Delete always asks; the yes line says what goes with it
static tui_menu * puzzlemenu_create_delconfirm(tui_menu_item_data * data, tui_menu * parent, tui_menu_unit_t pos) {
  PuzzleMenu * pm = data->ptr;
  int sel = pm->root.current - PMENU_FIRST;
  if(sel < 0 || sel >= pm->nentries) return NULL;
  PuzzleEntry * e = pm->entries + sel;
  char * yes = pm->delconfirm.items[1].name;
  size_t len = sizeof(pm->delconfirm.items[1].name);
  if(!e->hassave) snprintf(yes, len, "Yes, delete it");
  else if(e->save.groups <= 1) snprintf(yes, len, "Yes, delete it and its finished save");
  else {
    char time[16];
    textdraw_fmttime(time, sizeof(time), e->save.header.seconds);
    snprintf(yes, len, "Yes, delete it and its save (%lu left, %s)", (unsigned long)e->save.groups, time);
  }
  tui_menu_reset_ui(&pm->delconfirm);
  return &pm->delconfirm;
}

// Overlay the picture should show: the save's on its Continue line, otherwise the setting
static u8 puzzlemenu_thumboverlay(PuzzleMenu * pm, const PuzzleEntry * e) {
  int opt = pm->options.current;
  if(e->hassave && opt >= 0 && opt < pm->options.numitems && pm->optact[opt] == PMENU_ACT_CONTINUE) {
    return e->save.header.overlay;
  }
  return (unsigned)pm->overlay % OVERLAYS;
}

// Options are rebuilt for whichever puzzle was clicked
static tui_menu * puzzlemenu_create_options(tui_menu_item_data * data, tui_menu * parent, tui_menu_unit_t pos) {
  PuzzleMenu * pm = data->ptr;
  pos -= PMENU_FIRST;
  if(pos < 0 || pos >= pm->nentries) return NULL;
  PuzzleEntry * e = pm->entries + pos;
  tui_menu_clear(&pm->options);
  memset(pm->optcut, 0, sizeof(pm->optcut));
  pm->optwhimsy = -1;
  int err = 0, n = 0;
  char name[TUIMENU_MAXNAME];
  char path[STORAGE_MAXPATH];
  storage_puzzlepath(e->name, path);
  thumb_request(&pm->thumb, path, e->hassave ? e->save.header.overlay : (unsigned)pm->overlay % OVERLAYS);
  if(e->hassave) {
    char time[16];
    textdraw_fmttime(time, sizeof(time), e->save.header.seconds);
    if(e->save.groups <= 1) {
      snprintf(name, sizeof(name), "View finished puzzle (%s)", time);
    } else {
      snprintf(name, sizeof(name), "Continue: %lu left, %s", (unsigned long)e->save.groups, time);
    }
    TUIMITEM_BASIC(&pm->options, err, name, 0);
    pm->optact[n++] = PMENU_ACT_CONTINUE;
  }
  // Settings for a new puzzle (Continue uses the save's), then the button that starts it
  if(!err) {
    TUIMITEM_ENUM(&pm->options, err, "Pieces", &pm->pieces, PMENU_PIECES_NAMES);
    pm->optact[n++] = PMENU_ACT_NONE;
  }
  if(!err) {
    TUIMITEM_ENUM(&pm->options, err, "Piece style", &pm->style, "Classic\nIrregular\nWild\nRandom\nRandom Rect\nMaze");
    pm->optcut[n] = 1;
    pm->optact[n++] = PMENU_ACT_NONE;
  }
  if(!err) {
    TUIMITEM_ENUM(&pm->options, err, "Whimsies", &pm->whimsy, "None\nFew\nMany");
    pm->optwhimsy = n;
    pm->optcut[n] = 1;
    pm->optact[n++] = PMENU_ACT_NONE;
  }
  if(!err) {
    TUIMITEM_ENUM(&pm->options, err, "Overlay", &pm->overlay, PMENU_OVERLAY_NAMES);
    pm->optact[n++] = PMENU_ACT_NONE;
  }
  if(!err) {
    TUIMITEM_ENUM(&pm->options, err, "Music", &pm->music, PMENU_MUSIC_NAMES);
    pm->optact[n++] = PMENU_ACT_NONE;
  }
  if(!err) {
    tui_menu_item_data * idata;
    TUIMITEM_SUBMENU(&pm->options, err, "Start new puzzle", puzzlemenu_create_confirm,
        tui_menu_submenu_destroy_existing_menu, idata, 0);
    if(!err) idata->ptr = pm;
    pm->optact[n++] = PMENU_ACT_NEW;
  }
  if(!err) {
    TUIMITEM_BASIC(&pm->options, err, "Rename picture", 0);
    pm->optact[n++] = PMENU_ACT_RENAME;
  }
  if(!err) {
    tui_menu_item_data * idata;
    TUIMITEM_SUBMENU(&pm->options, err, "Delete puzzle", puzzlemenu_create_delconfirm,
        tui_menu_submenu_destroy_existing_menu, idata, 0);
    if(!err) idata->ptr = pm;
    pm->optact[n++] = PMENU_ACT_NONE;   // Acted on from delconfirm
  }
  return err ? NULL : &pm->options;
}

static void puzzlemenu_build_root(PuzzleMenu * pm) {
  tui_menu_unit_t cur = pm->root.current;
  tui_menu_clear(&pm->root);
  int err = 0;
  TUIMITEM_BASIC(&pm->root, err, "+ Add files over Wi-Fi", 0);
  if(pm->nentries == 0) {
    // littlemenu can't cope with an empty menu, so always have something
    TUIMITEM_BASIC(&pm->root, err, "(no images in /3ds/jigsaw/puzzles)", 0);
    return;
  }
  for(int i = 0; i < pm->nentries && !err; i++) {
    PuzzleEntry * e = pm->entries + i;
    char name[TUIMENU_MAXNAME];
    int len = puzzlemenu_namelen(e->name);
    if(len > 100) len = 100;
    if(e->hassave && e->save.groups <= 1) snprintf(name, sizeof(name), "%.*s  [done]", len, e->name);
    else if(e->hassave) snprintf(name, sizeof(name), "%.*s  [%lu left]", len, e->name, (unsigned long)e->save.groups);
    else snprintf(name, sizeof(name), "%.*s", len, e->name);
    tui_menu_item_data * idata;
    TUIMITEM_SUBMENU(&pm->root, err, name, puzzlemenu_create_options,
        tui_menu_submenu_destroy_existing_menu, idata, 0);
    if(!err) idata->ptr = pm;
  }
  // The cursor starts on the first puzzle, not on "Add pictures"; past the end (the last
  // one was deleted) it goes to the new last one
  if(cur <= 0 && pm->nentries) cur = PMENU_FIRST;
  if(cur >= pm->nentries + PMENU_FIRST) cur = pm->nentries + PMENU_FIRST - 1;
  if(cur > 0) {
    tui_menu_run(&pm->root, (tui_menu_action) { .action = TUIMENU_ACTION_POSITION, .offset = cur });
  }
}

int puzzlemenu_refresh(PuzzleMenu * pm) {
  free(pm->entries);
  pm->entries = NULL;
  pm->nentries = 0;
  int max = 0;
  DIR * d = opendir(STORAGE_PUZZLES);
  if(d) {
    struct dirent * de;
    while((de = readdir(d))) {
      if(de->d_name[0] == '.' || !puzzlemenu_isimage(de->d_name)) continue;
      if(strlen(de->d_name) >= PMENU_MAXNAME) continue;
      if(pm->nentries >= max) {
        max = max ? max * 2 : 16;
        PuzzleEntry * ne = realloc(pm->entries, sizeof(PuzzleEntry) * max);
        if(!ne) break;
        pm->entries = ne;
      }
      PuzzleEntry * e = pm->entries + pm->nentries++;
      memset(e, 0, sizeof(PuzzleEntry));
      snprintf(e->name, sizeof(e->name), "%s", de->d_name);
    }
    closedir(d);
  }
  if(pm->nentries) qsort(pm->entries, pm->nentries, sizeof(PuzzleEntry), puzzlemenu_cmp);
  for(int i = 0; i < pm->nentries; i++) {
    PuzzleEntry * e = pm->entries + i;
    char path[STORAGE_MAXPATH];
    uint8_t head[PMENU_PEEKBYTES];
    size_t len;
    storage_savepath(e->name, path);
    e->hassave = !storage_readhead(path, head, sizeof(head), &len) && savefile_peek(head, len, &e->save) == SAVEFILE_OK;
  }
  puzzlemenu_build_root(pm);
  return 0;
}

int puzzlemenu_init(PuzzleMenu * pm) {
  memset(pm, 0, sizeof(PuzzleMenu));
  tui_menu_init(&pm->root, PMENU_HEIGHT);
  tui_menu_init(&pm->options, PMENU_HEIGHT);
  tui_menu_init(&pm->confirm, PMENU_HEIGHT);
  tui_menu_init(&pm->delconfirm, PMENU_HEIGHT);
  pm->root.valuewidth = 0;
  pm->options.valuewidth = PMENU_VALUEWIDTH;
  pm->confirm.valuewidth = 0;
  pm->delconfirm.valuewidth = 0;
  pm->music = PMENU_MUSIC_DEFAULT;
  pm->pieces = PMENU_PIECES_DEFAULT;
  thumb_init(&pm->thumb);  // On failure there's just no picture
  int err = 0;
  TUIMITEM_BASIC(&pm->confirm, err, "No, keep my save", 1);
  if(!err) TUIMITEM_BASIC(&pm->confirm, err, "Yes, erase it and start over", 0);  // Reworded on open
  if(!err) TUIMITEM_BASIC(&pm->delconfirm, err, "No, keep it", 1);
  if(!err) TUIMITEM_BASIC(&pm->delconfirm, err, "Yes, delete it", 0);           // Reworded on open
  if(err) return err;
  return puzzlemenu_refresh(pm);
}

void puzzlemenu_free(PuzzleMenu * pm) {
  thumb_free(&pm->thumb);
  tui_menu_free(&pm->root);
  tui_menu_free(&pm->options);
  tui_menu_free(&pm->confirm);
  tui_menu_free(&pm->delconfirm);
  free(pm->entries);
  pm->entries = NULL;
}

void puzzlemenu_waitidle(PuzzleMenu * pm) {
  thumb_clear(&pm->thumb, 1);
}

void puzzlemenu_setstatus(PuzzleMenu * pm, const char * msg) {
  snprintf(pm->status, sizeof(pm->status), "%s", msg ? msg : "");
}

void puzzlemenu_select(PuzzleMenu * pm, const char * name) {
  for(int i = 0; i < pm->nentries; i++) {
    if(strcmp(pm->entries[i].name, name)) continue;
    tui_menu_reset(&pm->root);
    tui_menu_run(&pm->root, (tui_menu_action) { .action = TUIMENU_ACTION_POSITION, .offset = i + PMENU_FIRST });
    return;
  }
}

// The menu that currently has the cursor
static tui_menu * puzzlemenu_active(PuzzleMenu * pm) {
  tui_menu * m = &pm->root;
  for(;;) {
    if(m->current < 0 || m->current >= m->numitems) return m;
    tui_menu_item * it = m->items + m->current;
    if(it->type != TUIMENU_TYPE_SUBMENU || !it->data.submenu.menu) return m;
    m = it->data.submenu.menu;
  }
}

// Cut sample over the picture: on Piece style / Whimsies (with the settings) or on Continue (with the
// save's). Returns whether it shows; asks thumb for it if so.
static int puzzlemenu_thumbcut(PuzzleMenu * pm, const PuzzleEntry * e) {
  int opt = pm->options.current;
  if(puzzlemenu_active(pm) != &pm->options || opt < 0 || opt >= pm->options.numitems) return 0;
  // Between the save's sample and the settings' one, the old lines would show the wrong
  // puzzle, so they hide until the new ones are ready; flipping through settings they stay up
  if(e->hassave && pm->optact[opt] == PMENU_ACT_CONTINUE) {
    thumb_cut(&pm->thumb, e->save.header.style, e->save.header.whimsy, e->save.header.whimsyset,
        e->save.header.target, pm->cutsrc != 1);
    pm->cutsrc = 1;
    return 1;
  }
  if(!pm->optcut[opt]) return 0;
  thumb_cut(&pm->thumb, (unsigned)pm->style % CUT_STYLES, (unsigned)pm->whimsy % WHIMSY_LEVELS, WHIMSY_SET_CUTE,
      pmenu_counts[(unsigned)pm->pieces % PMENU_NUMCOUNTS], pm->cutsrc != 2);
  pm->cutsrc = 2;
  return 1;
}

// Options item that's grayed out and can't be changed: Whimsies while the style is Maze
// (maze cuts have none)
static int puzzlemenu_optoff(PuzzleMenu * pm, int item) {
  return item >= 0 && item == pm->optwhimsy && (unsigned)pm->style % CUT_STYLES == CUT_STYLE_MAZE;
}

PuzzleChoice puzzlemenu_update(PuzzleMenu * pm, u32 down, u32 repeat) {
  PuzzleChoice choice = { .name = NULL, .target = 0, .quit = 0 };
  tui_menu_action act = { .action = TUIMENU_ACTION_NONE, .offset = 0 };
  if(repeat & KEY_DUP) act = (tui_menu_action) { .action = TUIMENU_ACTION_MOVE, .offset = -1 };
  else if(repeat & KEY_DDOWN) act = (tui_menu_action) { .action = TUIMENU_ACTION_MOVE, .offset = 1 };
  else if(repeat & KEY_L) act = (tui_menu_action) { .action = TUIMENU_ACTION_MOVE, .offset = -PMENU_HEIGHT };
  else if(repeat & KEY_R) act = (tui_menu_action) { .action = TUIMENU_ACTION_MOVE, .offset = PMENU_HEIGHT };
  else if(repeat & KEY_DLEFT) act = (tui_menu_action) { .action = TUIMENU_ACTION_VALUE, .offset = -1 };
  else if(repeat & KEY_DRIGHT) act = (tui_menu_action) { .action = TUIMENU_ACTION_VALUE, .offset = 1 };
  else if(down & KEY_A) {
    // A on an option value (piece style) just flips it, like Right would
    tui_menu * m = puzzlemenu_active(pm);
    int isenum = m->current >= 0 && m->current < m->numitems && m->items[m->current].type == TUIMENU_TYPE_ENUM;
    act = isenum ? (tui_menu_action) { .action = TUIMENU_ACTION_VALUE, .offset = 1 } :
                   (tui_menu_action) { .action = TUIMENU_ACTION_ACCEPT, .offset = 0 };
  }
  else if(down & KEY_B) {
    // Cancel at the root would "exit" the menu; there's nothing above it, so ignore
    if(puzzlemenu_active(pm) != &pm->root) act = TUIMENU_CANCELACTION;
  }
  if(down & KEY_START) {
    choice.quit = 1;
    return choice;
  }
  if(act.action == TUIMENU_ACTION_VALUE && puzzlemenu_active(pm) == &pm->options && puzzlemenu_optoff(pm, pm->options.current)) {
    act.action = TUIMENU_ACTION_NONE;
  }
  if(act.action == TUIMENU_ACTION_NONE) return choice;
  if(act.action == TUIMENU_ACTION_ACCEPT || act.action == TUIMENU_ACTION_CANCEL) pm->status[0] = 0;

  tui_menu_result r = tui_menu_run(&pm->root, act);
  // Backed out to the list: the picture goes away. In the options it follows the overlay.
  if(pm->thumb.state != THUMB_NONE && puzzlemenu_active(pm) == &pm->root) thumb_clear(&pm->thumb, 0);
  int ent0 = pm->root.current - PMENU_FIRST;
  if(pm->thumb.state != THUMB_NONE && ent0 >= 0 && ent0 < pm->nentries) {
    u8 want = puzzlemenu_thumboverlay(pm, pm->entries + ent0);
    if(want != pm->thumb.overlay) {
      char path[STORAGE_MAXPATH];
      storage_puzzlepath(pm->entries[ent0].name, path);
      thumb_request(&pm->thumb, path, want);
    }
  }
  pm->showcut = pm->thumb.state != THUMB_NONE && ent0 >= 0 && ent0 < pm->nentries &&
      puzzlemenu_thumbcut(pm, pm->entries + ent0);
  if(!(act.action & TUIMENU_ACTION_ACCEPT) || r.cancelled || r.result < 0) return choice;
  tui_menu * active = puzzlemenu_active(pm);
  int sel = pm->root.current, ent = sel - PMENU_FIRST;
  if(active == &pm->root && sel == 0) {
    choice.upload = 1;
    return choice;
  }
  if(ent < 0 || ent >= pm->nentries) return choice;
  if(active == &pm->delconfirm) {
    if(pm->delconfirm.current != 1) return choice;
    choice.name = pm->entries[ent].name;
    choice.del = 1;
    thumb_clear(&pm->thumb, 0);  // Caller waits (puzzlemenu_waitidle) before deleting
    tui_menu_reset(&pm->root);
    tui_menu_run(&pm->root, (tui_menu_action) { .action = TUIMENU_ACTION_POSITION, .offset = sel });
    return choice;
  }
  int opt = pm->options.current;
  // Accepted inside options (no save, so no confirm) or "Yes" inside confirm
  if(active == &pm->options || (active == &pm->confirm && pm->confirm.current == 1)) {
    if(opt < 0 || opt >= pm->options.numitems || pm->optact[opt] == PMENU_ACT_NONE) return choice;
    choice.name = pm->entries[ent].name;
    choice.rename = pm->optact[opt] == PMENU_ACT_RENAME;
    choice.target = pm->optact[opt] == PMENU_ACT_NEW ? pmenu_counts[(unsigned)pm->pieces % PMENU_NUMCOUNTS] : 0;
    choice.style = pm->style;
    choice.whimsy = (unsigned)pm->whimsy % WHIMSY_LEVELS;
    choice.whimsyset = WHIMSY_SET_CUTE;   // The only set for now (no menu option yet)
    choice.overlay = (unsigned)pm->overlay % OVERLAYS;
    static const uint8_t modes[] = { SAVEFILE_MUSIC_OFF, SAVEFILE_MUSIC_ORDER, SAVEFILE_MUSIC_SHUFFLE };
    choice.music = modes[(unsigned)pm->music % 3];
    thumb_clear(&pm->thumb, 0);  // Caller waits (puzzlemenu_waitidle) once Loading is showing
    tui_menu_reset(&pm->root);
    // Keep the cursor on this puzzle when we come back
    tui_menu_run(&pm->root, (tui_menu_action) { .action = TUIMENU_ACTION_POSITION, .offset = sel });
  }
  return choice;
}

void puzzlemenu_render_top(PuzzleMenu * pm, TextDraw * td) {
  const u32 white = C2D_Color32(230, 230, 230, 255), sel = C2D_Color32(20, 20, 20, 255);
  const u32 selbg = C2D_Color32(240, 200, 90, 255), title = C2D_Color32(240, 200, 90, 255);
  const u32 off = C2D_Color32(110, 110, 110, 255), offsel = C2D_Color32(130, 100, 40, 255);  // Grayed out item
  tui_menu * active = puzzlemenu_active(pm);
  const char * heading = active == &pm->root ? "Pick a puzzle" :
      active == &pm->confirm ? "Erase the saved game?" : pm->entries[pm->root.current - PMENU_FIRST].name;
  int hlen = active == &pm->root || active == &pm->confirm ? (int)strlen(heading) : puzzlemenu_namelen(heading);
  if(active == &pm->delconfirm) textdraw_print(td, 10, 6, 0.6f, title, "Delete %.*s?", hlen, heading);
  else textdraw_print(td, 10, 6, 0.6f, title, "%.*s", hlen, heading);
  // The puzzle's disk use (picture + save + caches), small in the corner like the version
  if(active != &pm->root && pm->thumb.hasbytes) {
    uint64_t b = pm->thumb.bytes;
    const u32 dim = C2D_Color32(140, 140, 140, 255);
    if(b < 1000 * 1024) textdraw_right(td, 394, 224, 0.4f, dim, "%lu KB", (unsigned long)((b + 1023) / 1024));
    else textdraw_right(td, 394, 224, 0.4f, dim, "%.1f MB", b / (1024.0 * 1024.0));
  }
  char line[PMENU_WIDTH + 1];
  for(int i = 0; i < PMENU_HEIGHT; i++) {
    tui_menu_renderline(&pm->root, line, PMENU_WIDTH, i);
    float y = 32 + i * 16;
    int cur = tui_menu_iscurrent(&pm->root, i) && active->numitems > 0 && active->top + i < active->numitems;
    if(cur) C2D_DrawRectSolid(6, y, 0.5f, 388, 16, selbg);
    u32 fg = cur ? sel : white;
    if(active == &pm->options && puzzlemenu_optoff(pm, active->top + i)) fg = cur ? offsel : off;
    // littlemenu pads to character columns, but the system font is proportional, so the
    // value column gets its own fixed x or values drift with the label widths
    int split = PMENU_WIDTH - active->valuewidth;
    if(active->valuewidth > 0 && split > 0) {
      textdraw_print(td, 10, y + 1, 0.5f, fg, "%.*s", split, line);
      textdraw_print(td, PMENU_VALUEX, y + 1, 0.5f, fg, "%s", line + split);
    } else {
      textdraw_print(td, 10, y + 1, 0.5f, fg, "%s", line);
    }
  }
}

void puzzlemenu_render_bottom(PuzzleMenu * pm, TextDraw * td) {
  const u32 white = C2D_Color32(220, 220, 220, 255), dim = C2D_Color32(140, 140, 140, 255);
  const u32 warn = C2D_Color32(255, 140, 120, 255);
  int ts = thumb_poll(&pm->thumb);
  if(ts != THUMB_NONE) {
    // Picture fills the screen above a strip of controls. Nothing shows while it loads.
    if(ts == THUMB_READY) {
      // With the cut sample, only the part the puzzle keeps (its crop), the cut over it once
      // it matches the settings (an older sample's crop stays meanwhile)
      int cut = pm->showcut && thumb_cutready(&pm->thumb);
      C2D_Image img = cut ? thumb_cropimage(&pm->thumb) : thumb_image(&pm->thumb);
      float x = (320 - img.subtex->width) / 2, y = (PMENU_THUMBH - img.subtex->height) / 2;
      C2D_DrawImageAt(img, x, y, 0.5f, NULL, 1, 1);
      if(cut && thumb_cutcurrent(&pm->thumb)) C2D_DrawImageAt(thumb_cutimage(&pm->thumb), x, y, 0.5f, NULL, 1, 1);
    } else if(ts == THUMB_FAILED) {
      textdraw_center(td, 160, PMENU_THUMBH / 2 - 8, 0.6f, dim, "Can't preview");
    }
    textdraw_print(td, 8, PMENU_THUMBH + 4, 0.5f, dim, PMENU_HELP_MOVE);
    textdraw_print(td, 8, PMENU_THUMBH + 20, 0.5f, dim, "Left/Right: change option");
    return;
  }
  textdraw_center(td, 160, 20, 0.9f, white, "3DS Jigsaw");
  textdraw_print(td, 12, 70, 0.5f, dim, "Add pictures over Wi-Fi (top of the list),");
  textdraw_print(td, 12, 86, 0.5f, dim, "or put them in");
  textdraw_print(td, 100, 86, 0.5f, white, "sdmc:/3ds/jigsaw/puzzles/");
  textdraw_print(td, 12, 116, 0.5f, dim, PMENU_HELP_MOVE);
  textdraw_print(td, 12, 132, 0.5f, dim, "Left/Right: change option   L/R: page");
  textdraw_print(td, 12, 148, 0.5f, dim, "START: quit");
  if(pm->status[0]) textdraw_print(td, 12, 180, 0.5f, warn, "%s", pm->status);
  textdraw_print(td, 270, 222, 0.4f, dim, "v" APP_VERSION_STRING);
}

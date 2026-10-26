#include "puzzlemenu.h"

#include <citro2d.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const uint32_t pmenu_counts[PMENU_NUMCOUNTS] = { 48, 108, 252, 500 };

static int puzzlemenu_isimage(const char * name) {
  const char * dot = strrchr(name, '.');
  if(!dot) return 0;
  return !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg") ||
         !strcasecmp(dot, ".png") || !strcasecmp(dot, ".bmp");
}

static int puzzlemenu_cmp(const void * a, const void * b) {
  return strcasecmp(((const PuzzleEntry *)a)->name, ((const PuzzleEntry *)b)->name);
}

// Confirm menu only appears when there's a save to throw away. Returning NULL makes
// littlemenu treat the item as a plain click.
static tui_menu * puzzlemenu_create_confirm(tui_menu_item_data * data, tui_menu * parent, tui_menu_unit_t pos) {
  PuzzleMenu * pm = data->ptr;
  int sel = pm->root.current;
  if(sel < 0 || sel >= pm->nentries || !pm->entries[sel].hassave) return NULL;
  tui_menu_reset_ui(&pm->confirm);
  return &pm->confirm;
}

// Options are rebuilt for whichever puzzle was clicked
static tui_menu * puzzlemenu_create_options(tui_menu_item_data * data, tui_menu * parent, tui_menu_unit_t pos) {
  PuzzleMenu * pm = data->ptr;
  if(pos < 0 || pos >= pm->nentries) return NULL;
  PuzzleEntry * e = pm->entries + pos;
  tui_menu_clear(&pm->options);
  int err = 0, n = 0;
  char name[TUIMENU_MAXNAME];
  if(e->hassave) {
    if(e->save.groups <= 1) {
      snprintf(name, sizeof(name), "View finished puzzle (%lu pieces)", (unsigned long)e->save.pieces);
    } else {
      snprintf(name, sizeof(name), "Continue (%lu pieces, %lu groups left)",
          (unsigned long)e->save.pieces, (unsigned long)e->save.groups);
    }
    TUIMITEM_BASIC(&pm->options, err, name, 0);
    pm->optact[n++] = PMENU_ACT_CONTINUE;
  }
  for(int i = 0; i < PMENU_NUMCOUNTS && !err; i++) {
    snprintf(name, sizeof(name), "New: %lu pieces", (unsigned long)pmenu_counts[i]);
    tui_menu_item_data * idata;
    TUIMITEM_SUBMENU(&pm->options, err, name, puzzlemenu_create_confirm,
        tui_menu_submenu_destroy_existing_menu, idata, 0);
    if(!err) idata->ptr = pm;
    pm->optact[n++] = pmenu_counts[i];
  }
  return err ? NULL : &pm->options;
}

static void puzzlemenu_build_root(PuzzleMenu * pm) {
  tui_menu_unit_t cur = pm->root.current;
  tui_menu_clear(&pm->root);
  int err = 0;
  if(pm->nentries == 0) {
    // littlemenu can't cope with an empty menu, so always have something
    TUIMITEM_BASIC(&pm->root, err, "(no images in /3ds/jigsaw/puzzles)", 0);
    return;
  }
  for(int i = 0; i < pm->nentries && !err; i++) {
    PuzzleEntry * e = pm->entries + i;
    char name[TUIMENU_MAXNAME];
    if(e->hassave && e->save.groups <= 1) snprintf(name, sizeof(name), "%s  [done]", e->name);
    else if(e->hassave) snprintf(name, sizeof(name), "%s  [%lu left]", e->name, (unsigned long)e->save.groups);
    else snprintf(name, sizeof(name), "%s", e->name);
    tui_menu_item_data * idata;
    TUIMITEM_SUBMENU(&pm->root, err, name, puzzlemenu_create_options,
        tui_menu_submenu_destroy_existing_menu, idata, 0);
    if(!err) idata->ptr = pm;
  }
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
    pm->entries[i].hassave = storage_peeksave(pm->entries[i].name, &pm->entries[i].save) == 0;
  }
  puzzlemenu_build_root(pm);
  return 0;
}

int puzzlemenu_init(PuzzleMenu * pm) {
  memset(pm, 0, sizeof(PuzzleMenu));
  tui_menu_init(&pm->root, PMENU_HEIGHT);
  tui_menu_init(&pm->options, PMENU_HEIGHT);
  tui_menu_init(&pm->confirm, PMENU_HEIGHT);
  pm->root.valuewidth = 0;
  pm->options.valuewidth = 0;
  pm->confirm.valuewidth = 0;
  int err = 0;
  TUIMITEM_BASIC(&pm->confirm, err, "No, keep my save", 1);
  if(!err) TUIMITEM_BASIC(&pm->confirm, err, "Yes, erase it and start over", 0);
  if(err) return err;
  return puzzlemenu_refresh(pm);
}

void puzzlemenu_free(PuzzleMenu * pm) {
  tui_menu_free(&pm->root);
  tui_menu_free(&pm->options);
  tui_menu_free(&pm->confirm);
  free(pm->entries);
  pm->entries = NULL;
}

void puzzlemenu_setstatus(PuzzleMenu * pm, const char * msg) {
  snprintf(pm->status, sizeof(pm->status), "%s", msg ? msg : "");
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

PuzzleChoice puzzlemenu_update(PuzzleMenu * pm, u32 down, u32 repeat) {
  PuzzleChoice choice = { .name = NULL, .target = 0, .quit = 0 };
  tui_menu_action act = { .action = TUIMENU_ACTION_NONE, .offset = 0 };
  if(repeat & KEY_DUP) act = (tui_menu_action) { .action = TUIMENU_ACTION_MOVE, .offset = -1 };
  else if(repeat & KEY_DDOWN) act = (tui_menu_action) { .action = TUIMENU_ACTION_MOVE, .offset = 1 };
  else if(repeat & KEY_DLEFT) act = (tui_menu_action) { .action = TUIMENU_ACTION_MOVE, .offset = -PMENU_HEIGHT };
  else if(repeat & KEY_DRIGHT) act = (tui_menu_action) { .action = TUIMENU_ACTION_MOVE, .offset = PMENU_HEIGHT };
  else if(down & KEY_A) act = (tui_menu_action) { .action = TUIMENU_ACTION_ACCEPT, .offset = 0 };
  else if(down & KEY_B) {
    // Cancel at the root would "exit" the menu; there's nothing above it, so ignore
    if(puzzlemenu_active(pm) != &pm->root) act = TUIMENU_CANCELACTION;
  }
  if(down & KEY_START) {
    choice.quit = 1;
    return choice;
  }
  if(act.action == TUIMENU_ACTION_NONE) return choice;
  if(act.action != TUIMENU_ACTION_MOVE) pm->status[0] = 0;

  tui_menu_result r = tui_menu_run(&pm->root, act);
  if(!(act.action & TUIMENU_ACTION_ACCEPT) || r.cancelled || r.result < 0) return choice;
  tui_menu * active = puzzlemenu_active(pm);
  int sel = pm->root.current;
  if(sel < 0 || sel >= pm->nentries) return choice;
  int opt = pm->options.current;
  // Accepted inside options (no save, so no confirm) or "Yes" inside confirm
  if(active == &pm->options || (active == &pm->confirm && pm->confirm.current == 1)) {
    if(opt < 0 || opt >= pm->options.numitems) return choice;
    choice.name = pm->entries[sel].name;
    choice.target = pm->optact[opt];
    tui_menu_reset(&pm->root);
    // Keep the cursor on this puzzle when we come back
    tui_menu_run(&pm->root, (tui_menu_action) { .action = TUIMENU_ACTION_POSITION, .offset = sel });
  }
  return choice;
}

void puzzlemenu_render_top(PuzzleMenu * pm, TextDraw * td) {
  const u32 white = C2D_Color32(230, 230, 230, 255), sel = C2D_Color32(20, 20, 20, 255);
  const u32 selbg = C2D_Color32(240, 200, 90, 255), title = C2D_Color32(240, 200, 90, 255);
  tui_menu * active = puzzlemenu_active(pm);
  const char * heading = active == &pm->root ? "Pick a puzzle" :
      active == &pm->options ? pm->entries[pm->root.current].name : "Erase the saved game?";
  textdraw_print(td, 10, 6, 0.6f, title, "%s", heading);
  char line[PMENU_WIDTH + 1];
  for(int i = 0; i < PMENU_HEIGHT; i++) {
    tui_menu_renderline(&pm->root, line, PMENU_WIDTH, i);
    float y = 32 + i * 16;
    int cur = tui_menu_iscurrent(&pm->root, i) && active->numitems > 0 && active->top + i < active->numitems;
    if(cur) C2D_DrawRectSolid(6, y, 0.5f, 388, 16, selbg);
    textdraw_print(td, 10, y + 1, 0.5f, cur ? sel : white, "%s", line);
  }
}

void puzzlemenu_render_bottom(PuzzleMenu * pm, TextDraw * td) {
  const u32 white = C2D_Color32(220, 220, 220, 255), dim = C2D_Color32(140, 140, 140, 255);
  const u32 warn = C2D_Color32(255, 140, 120, 255);
  textdraw_center(td, 160, 20, 0.9f, white, "3DS Jigsaw");
  textdraw_print(td, 12, 70, 0.5f, dim, "Put .jpg/.png images in");
  textdraw_print(td, 12, 86, 0.5f, white, "  sdmc:/3ds/jigsaw/puzzles/");
  textdraw_print(td, 12, 116, 0.5f, dim, "D-Pad: move   A: select   B: back");
  textdraw_print(td, 12, 132, 0.5f, dim, "START: quit");
  if(pm->status[0]) textdraw_print(td, 12, 180, 0.5f, warn, "%s", pm->status);
}

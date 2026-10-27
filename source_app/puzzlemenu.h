#ifndef __HEADER_JIGSAW_PUZZLEMENU__
#define __HEADER_JIGSAW_PUZZLEMENU__

#include <3ds.h>

#include "littlemenu.h"
#include "savefile.h"
#include "storage.h"
#include "textdraw.h"
#include "thumb.h"

#define PMENU_HEIGHT 12
#define PMENU_WIDTH 46
#define PMENU_MAXNAME 128
#define PMENU_NUMCOUNTS 4
#define PMENU_PIECES_NAMES "50\n100\n250\n500"  // Same as pmenu_counts in puzzlemenu.c
#define PMENU_PIECES_DEFAULT 1                  // 100
#define PMENU_MAXOPTS 5      // Continue, Pieces, Piece style, Music, Start new puzzle
#define PMENU_ACT_CONTINUE 0
#define PMENU_ACT_NEW 0xFFFFFFFE
#define PMENU_ACT_NONE 0xFFFFFFFF
#define PMENU_VALUEWIDTH 9   // Room for the piece style value in the options menu
#define PMENU_VALUEX 200     // Pixel x of that value column (top screen)
#define PMENU_MUSIC_NAMES "Off\nIn order\nShuffled"  // Menu order of the music modes
#define PMENU_MUSIC_DEFAULT 1                         // In order
#define PMENU_THUMBH THUMB_MAXH  // Bottom screen height used by the picture; controls below
#define PMENU_PEEKBYTES 256     // Start of a save read to summarize it (header + board header)

typedef struct {
  char name[PMENU_MAXNAME];
  SaveInfo save;
  u8 hassave;
} PuzzleEntry;

// What the player picked
typedef struct {
  const char * name;    // NULL = nothing picked yet
  uint32_t target;      // Piece count for a new game, 0 = continue
  uint8_t style;        // Cut style for a new game (CUT_STYLE_*)
  uint8_t music;        // Music mode for a new game (SAVEFILE_MUSIC_*)
  u8 quit;
} PuzzleChoice;

// Puzzle list -> options (continue / new-puzzle settings / start new) -> confirm overwrite,
// using littlemenu.
typedef struct {
  tui_menu root;
  tui_menu options;
  tui_menu confirm;
  PuzzleEntry * entries;
  int nentries;
  uint32_t optact[PMENU_MAXOPTS];        // Action for each options item
  tui_menu_number_t pieces;              // Piece count picked in options: index into the counts
  tui_menu_number_t style;               // Piece style picked in options (kept between puzzles)
  tui_menu_number_t music;               // Music picked in options: PMENU_MUSIC_* order
  char status[128];                      // Shown under the menu (errors etc.)
  Thumb thumb;                           // Picture of the puzzle being set up (bottom screen)
} PuzzleMenu;

int puzzlemenu_init(PuzzleMenu * pm);
void puzzlemenu_free(PuzzleMenu * pm);
// Rescan the puzzles folder and saves; keeps the cursor where it was
int puzzlemenu_refresh(PuzzleMenu * pm);
void puzzlemenu_setstatus(PuzzleMenu * pm, const char * msg);
// Wait for the thumbnail worker to finish (its decode memory is needed to load a game).
// Can take a second or two on an Old 3DS, so show something first.
void puzzlemenu_waitidle(PuzzleMenu * pm);
PuzzleChoice puzzlemenu_update(PuzzleMenu * pm, u32 down, u32 repeat);
void puzzlemenu_render_top(PuzzleMenu * pm, TextDraw * td);
void puzzlemenu_render_bottom(PuzzleMenu * pm, TextDraw * td);

#endif

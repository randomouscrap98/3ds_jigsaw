#ifndef __HEADER_JIGSAW_GAME__
#define __HEADER_JIGSAW_GAME__

#include <3ds.h>

#include "board.h"
#include "camera.h"
#include "puzzlegfx.h"
#include "storage.h"
#include "textdraw.h"

#define GAME_TABLESCALE 3.0f      // Table is this many times the puzzle size each way
#define GAME_SNAP 0.3f            // Snap distance, in cells
#define GAME_MAXZOOM 2.0f
#define GAME_STARTCELL 28.0f      // Starting zoom shows a cell this many pixels wide
#define GAME_ZOOMSTEP 1.15f
#define GAME_PANSPEED 7.0f        // Pixels per frame at full circle pad
#define GAME_EDGEPAN 18           // Dragging this close to the screen edge pans
#define GAME_EDGEPANSPEED 4.0f
#define GAME_AUTOSAVE_MS 30000

#define GAME_EV_MERGE    (1 << 0)
#define GAME_EV_COMPLETE (1 << 1)
#define GAME_EV_EXIT     (1 << 2)

// Input for one frame, gathered by the caller
typedef struct {
  u32 down, held, up, repeat;
  touchPosition touch;
  circlePosition circle;
} GameInput;

// One puzzle being played: the board, the view onto it, its graphics, and the
// stylus state.
typedef struct {
  Board board;
  Camera cam;
  PuzzleGfx gfx;
  char name[256];
  uint32_t target;
  int32_t held;           // Group being dragged, or -1
  u8 panning;             // Stylus is dragging the table
  float lastwx, lastwy;   // World point under the stylus last frame
  float lastsx, lastsy;   // Screen point of the stylus last frame
  u8 dirty;
  u64 lastsave;
} Game;

// Start a puzzle. target = piece count for a new game, or 0 to continue the save.
int game_init(Game * g, const char * name, uint32_t target, progress_fn cb, void * udata,
    char * err, size_t errlen);
void game_free(Game * g);
// Returns GAME_EV_ flags
int game_update(Game * g, const GameInput * in);
void game_render_bottom(Game * g);
void game_render_top(Game * g, TextDraw * td);
int game_save(Game * g);

#endif

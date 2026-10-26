#ifndef __HEADER_JIGSAW_GAME__
#define __HEADER_JIGSAW_GAME__

#include <3ds.h>

#include "board.h"
#include "camera.h"
#include "stick.h"
#include "puzzlegfx.h"
#include "render.h"
#include "savewriter.h"
#include "storage.h"
#include "textdraw.h"

#define GAME_SCREENW 320          // Bottom screen, where the table is
#define GAME_SCREENH 240
#define GAME_TABLESCALE 3.0f      // Table is this many times the puzzle size each way
#define GAME_SCATTER 0.67f        // Pieces start spread over this fraction of the table (centered)
#define GAME_SNAP 0.3f            // Snap distance, in cells
#define GAME_MAXZOOM 2.0f
#define GAME_STARTCELL 28.0f      // Starting zoom shows a cell this many pixels wide
#define GAME_ZOOMSTEP 1.15f       // Per D-pad / X / B press or repeat
#define GAME_STICKZOOM 1.035f     // Per frame zoom factor at full C-stick tilt (New 3DS)
#define GAME_CIRCLE_RANGE 156     // Circle pad full tilt
#define GAME_CSTICK_RANGE 146     // C-stick full tilt (guess; values past it just clamp)
#define GAME_PANSPEED 15.0f       // Pixels per frame at full circle pad
#define GAME_PANMIN 1.0f          // Pixels per frame just past the deadzone
#define GAME_STICK_DEADZONE 35    // Raw stick value ignored (3DS sticks are noisy; junkdraw used 40)
#define GAME_STICK_CURVE 2.0f     // Speed rises with tilt^this between the min and the max
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
  circlePosition cstick;  // New 3DS only; always 0,0 elsewhere
} GameInput;

// One puzzle being played: the board, the view onto it, its graphics, and the
// stylus state.
typedef struct {
  Board board;
  Camera cam;
  Stick stick;
  Stick cstick;
  PuzzleGfx gfx;
  char name[256];
  uint32_t target;
  uint8_t style;          // Cut style (PGFX_STYLE_*)
  uint8_t table;          // Table color (palette index in render)
  SaveWriter * writer;    // Where saves go (NULL = write immediately)
  int32_t held;           // Group being dragged, or -1
  u8 panning;             // Stylus is dragging the table
  float lastwx, lastwy;   // World point under the stylus last frame
  float lastsx, lastsy;   // Screen point of the stylus last frame
  u8 dirty;
  u64 lastsave;
} Game;

// Start a puzzle. target = piece count for a new game (cut in the given style), or 0 to
// continue the save (style then comes from the save).
int game_init(Game * g, const char * name, uint32_t target, uint8_t style, SaveWriter * writer,
    progress_fn cb, void * udata,
    char * err, size_t errlen);
void game_free(Game * g);
// Returns GAME_EV_ flags
int game_update(Game * g, const GameInput * in);
void game_render_bottom(Game * g, Render * r);
void game_render_top(Game * g, TextDraw * td);
// Save the puzzle. background = hand it to the writer thread (autosave); otherwise write it
// now, after anything already queued so an older autosave can't land on top. 0 = success.
int game_save(Game * g, int background);

#endif

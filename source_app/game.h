#ifndef __HEADER_JIGSAW_GAME__
#define __HEADER_JIGSAW_GAME__

#include <3ds.h>

#include "board.h"
#include "camera.h"
#include "drawlist.h"
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
#define GAME_STICKZOOM 1.053f     // Per frame zoom factor at full C-stick tilt (New 3DS)
#define GAME_CIRCLE_RANGE 156     // Circle pad full tilt
#define GAME_CSTICK_RANGE 146     // C-stick full tilt (guess; values past it just clamp)
#define GAME_PANSPEED 15.0f       // Pixels per frame at full circle pad
#define GAME_PANMIN 1.0f          // Pixels per frame just past the deadzone
#define GAME_STICK_DEADZONE 35    // Raw stick value ignored (3DS sticks are noisy; junkdraw used 40)
#define GAME_STICK_CURVE 2.0f     // Speed rises with tilt^this between the min and the max
#define GAME_EDGEPAN 18           // Dragging this close to the screen edge pans
#define GAME_EDGEPANSPEED 4.0f
#define GAME_AUTOSAVE_MS 30000
#define GAME_MAXFRAME_MS 100      // Longer frames (HOME menu, sleep) only count this much play time
#define GAME_OVERLAY_FADE_MS 200  // Info overlay (hold Y) fade in / out time
#define GAME_MARQUEE_SPEED 30.0f  // Song name scroll, pixels per second
#define GAME_SELECTKEYS (KEY_A | KEY_DLEFT)  // Held at touch-down: box-select / tap-toggle
#define GAME_TAPDIST 6            // Stylus moving less than this (pixels) is a tap, not a box

#define GAME_EV_MERGE    (1 << 0)
#define GAME_EV_COMPLETE (1 << 1)
#define GAME_EV_EXIT     (1 << 2)
#define GAME_EV_SKIPSONG (1 << 3)
#define GAME_EV_MUSICMODE (1 << 4)  // g->music changed

#define GAME_MODKEY KEY_Y  // Modifier: held = overlay, buttons switch to the second set below
#define GAME_MOD_SKIPSONG KEY_X
#define GAME_MOD_MUSICMODE KEY_A  // Cycles off -> in order -> shuffled
#define GAME_MOD_CONDENSE KEY_SELECT  // Squish the selection into a tidy block
#define GAME_MOD_KEEPKEYS (KEY_DUP | KEY_DDOWN)  // Buttons that keep working with the modifier (zoom)

// Input for one frame, gathered by the caller
typedef struct {
  u32 down, held, up, repeat;
  touchPosition touch;
  circlePosition circle;
  circlePosition cstick;  // New 3DS only; always 0,0 elsewhere
} GameInput;

// Frame timings (ms, smoothed), measured by main
typedef struct {
  float frame;            // Loop start to loop start
  float cpu;              // Our CPU work: update + building the frame (not the vsync wait)
  float update;           // game_update
  float bottom;           // game_render_bottom (table + pieces)
  float music;            // Music thread decoding, per frame
  float musicsd;          // Music thread waiting on file reads, per frame
  char song[256];         // Music file playing ("" = none)
  uint32_t drawn, texswaps;
  float build;            // Last drawlist rebuild (not smoothed)
  uint32_t layers;        // Drawlist layers
} GamePerf;

// One puzzle being played: the board, the view onto it, its graphics, and the
// stylus state.
typedef struct {
  PieceGeo geo;           // Piece geometry (the board points at it)
  Board board;
  DrawList dl;            // Piece draw order (batched by texture page)
  u8 dldirty;             // Rebuild dl before the next draw
  uint32_t ntop;          // Groups on top of the board order that are moving (dl's ntop)
  Camera cam;
  Stick stick;
  Stick cstick;
  PuzzleGfx gfx;
  char name[256];
  uint32_t target;
  uint32_t seed;          // Cut seed (from the save, so a renamed picture cuts the same)
  uint8_t style;          // Cut style (CUT_STYLE_*)
  uint8_t whimsy;         // Whimsy level (WHIMSY_*)
  uint8_t whimsyset;      // Whimsy shapes (WHIMSY_SET_*)
  uint8_t picoverlay;     // Picture overlay (OVERLAY_*; not the hold-Y info overlay)
  uint8_t table;          // Table color (palette index in render)
  uint8_t music;          // Music mode (SAVEFILE_MUSIC_*), saved with the puzzle
  SaveWriter * writer;    // Where saves go (NULL = write immediately)
  int32_t held;           // Group being dragged, or -1
  u8 panning;             // Stylus is dragging the table
  u8 boxing;              // Stylus is drawing a selection box (or tapping to toggle)
  u8 movingsel;           // held is part of the selection: the whole selection follows
  uint8_t * sel;          // Selected groups: one byte per group slot (board.count)
  uint32_t nsel;          // Number of selected groups
  uint8_t * boxsel;       // Scratch: what the box being drawn covers (for display)
  float boxwx, boxwy;     // World point where the box started
  float boxsx, boxsy;     // Screen point where the box started
  float boxmove;          // Farthest the stylus got from the box start (pixels)
  float lastwx, lastwy;   // World point under the stylus last frame
  float lastsx, lastsy;   // Screen point of the stylus last frame
  u8 dirty;
  u8 savefailed;          // The last START save failed
  u64 lastsave;
  u64 lastframe;          // osGetTime of the previous update
  u64 playms;             // Play time; stops counting once complete
  float overlay;          // Info overlay opacity 0..1 (hold Y)
  GamePerf perf;          // Filled in by main, shown on the overlay
  char lastsong[256];     // Song the marquee is showing, and when it started
  u64 songstart;
} Game;

// Start a puzzle. target = piece count for a new game (cut in the given style, with the
// given music mode), or 0 to continue the save (style and music then come from the save).
int game_init(Game * g, const char * name, uint32_t target, uint8_t style, uint8_t whimsy, uint8_t whimsyset, uint8_t overlay,
    uint8_t music, SaveWriter * writer,
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

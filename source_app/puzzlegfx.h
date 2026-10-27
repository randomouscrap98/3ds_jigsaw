#ifndef __HEADER_JIGSAW_PUZZLEGFX__
#define __HEADER_JIGSAW_PUZZLEGFX__

#include <3ds.h>
#include <citro2d.h>

#include "layout.h"
#include "piecegeo.h"
#include "progress.h"

#define PGFX_MAXPAGES 3
#define PGFX_MAXSIDE 1024             // Puzzle image is scaled to fit this
#define PGFX_PREVIEW_TEXW 512
#define PGFX_PREVIEW_TEXH 256
#define PGFX_PREVIEW_MAXW 400
#define PGFX_PREVIEW_MAXH 240
#define PGFX_CACHE_VERSION 6     // 3: validated cuts, Wild style, 3 pages. 4: + PieceGeo.
                                 // 5: packed tiles (table in the cache), Random style.
                                 // 6: whimsies (+ level and set in the header), cut hash,
                                 //    struct sizes
#define PGFX_MAXGEO (4 << 20)    // Sanity limit on the cached PieceGeo size


// All the textures for one puzzle: piece tiles packed into pages, and a preview of the
// whole picture. Built from the source image on first use and cached to SD after.
typedef struct {
  Layout layout;
  uint32_t count;               // Pieces
  uint32_t cuthash;             // cutter_hash of the cut (saves check it on continue)
  LayoutTile * tiles;           // One per piece
  uint8_t * pageof;             // Texture page of each piece (for drawlist)
  C3D_Tex pages[PGFX_MAXPAGES];
  C3D_Tex preview;
  Tex3DS_SubTexture * subs;     // One per piece
  Tex3DS_SubTexture previewsub;
  uint16_t npages;              // Number of initialized page textures
  uint8_t haspreview;
} PuzzleGfx;

// Load (from cache if valid, otherwise build + write cache). seed picks the cut, style is
// CUT_STYLE_*, whimsy/set WHIMSY_* / WHIMSY_SET_*, overlay OVERLAY_*. The cut's piece geometry goes to geo (cached with the textures; the
// caller frees it). On failure, a human readable reason is written to err and geo is left
// empty. Returns 0 on success.
int puzzlegfx_load(PuzzleGfx * pg, PieceGeo * geo, const char * imagepath, const char * cachepath,
    uint32_t target, uint32_t seed, uint8_t style, uint8_t whimsy, uint8_t whimsyset, uint8_t overlay,
    progress_fn cb, void * udata, char * err, size_t errlen);
void puzzlegfx_free(PuzzleGfx * pg);
C2D_Image puzzlegfx_piece(PuzzleGfx * pg, uint32_t piece);
C2D_Image puzzlegfx_preview(PuzzleGfx * pg);

#endif

#ifndef __HEADER_JIGSAW_PUZZLEGFX__
#define __HEADER_JIGSAW_PUZZLEGFX__

#include <3ds.h>
#include <citro2d.h>

#include "layout.h"
#include "progress.h"

#define PGFX_MAXPAGES 2
#define PGFX_MAXSIDE 1024             // Puzzle image is scaled to fit this
#define PGFX_MAXSRCPIXELS (2100 * 2100) // Refuse to decode bigger images (memory)
#define PGFX_PREVIEW_TEXW 512
#define PGFX_PREVIEW_TEXH 256
#define PGFX_PREVIEW_MAXW 400
#define PGFX_PREVIEW_MAXH 240
#define PGFX_CACHE_VERSION 2

#define PGFX_STYLE_CLASSIC 0
#define PGFX_STYLE_IRREGULAR 1


// All the textures for one puzzle: piece tiles packed into pages, and a preview of the
// whole picture. Built from the source image on first use and cached to SD after.
typedef struct {
  Layout layout;
  C3D_Tex pages[PGFX_MAXPAGES];
  C3D_Tex preview;
  Tex3DS_SubTexture * subs;     // One per piece
  Tex3DS_SubTexture previewsub;
  uint16_t npages;              // Number of initialized page textures
  uint8_t haspreview;
} PuzzleGfx;

// Load (from cache if valid, otherwise build + write cache). seed picks the cut, style is
// PGFX_STYLE_*. On failure, a human readable reason is written to err. Returns 0 on success.
int puzzlegfx_load(PuzzleGfx * pg, const char * imagepath, const char * cachepath,
    uint32_t target, uint32_t seed, uint8_t style, progress_fn cb, void * udata, char * err, size_t errlen);
void puzzlegfx_free(PuzzleGfx * pg);
C2D_Image puzzlegfx_piece(PuzzleGfx * pg, uint32_t piece);
C2D_Image puzzlegfx_preview(PuzzleGfx * pg);

#endif

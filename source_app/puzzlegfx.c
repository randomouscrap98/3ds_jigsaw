#include "puzzlegfx.h"
#include "edgegen.h"
#include "imageload.h"
#include "imgscale.h"
#include "piecebake.h"
#include "storage.h"
#include "swizzle.h"
#include "texutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Raw struct on disk; only ever read back by the same build of the same program
typedef struct {
  char magic[4];
  u32 version;
  u64 srcsize;
  u64 srcmtime;
  u32 target;
  u32 seed;
  u32 style;
  Layout layout;
  u16 previeww, previewh;
  u32 geosize;            // PieceGeo blob after the preview (piecegeo_write)
} PuzzleCacheHeader;

#define PGFX_ERR(...) do { snprintf(err, errlen, __VA_ARGS__); } while(0)

static void puzzlegfx_zero(PuzzleGfx * pg) {
  memset(pg, 0, sizeof(PuzzleGfx));
}

void puzzlegfx_free(PuzzleGfx * pg) {
  for(int i = 0; i < pg->npages; i++) C3D_TexDelete(&pg->pages[i]);
  if(pg->haspreview) C3D_TexDelete(&pg->preview);
  free(pg->subs);
  puzzlegfx_zero(pg);
}

static int puzzlegfx_alloc(PuzzleGfx * pg, char * err, size_t errlen) {
  for(int i = 0; i < pg->layout.pages; i++) {
    if(texutil_init(&pg->pages[i], LAYOUT_PAGESIZE, LAYOUT_PAGESIZE, 1)) {
      PGFX_ERR("Out of video memory (page %d)", i);
      return 1;
    }
    pg->npages++;
  }
  if(texutil_init(&pg->preview, PGFX_PREVIEW_TEXW, PGFX_PREVIEW_TEXH, 0)) {
    PGFX_ERR("Out of video memory (preview)");
    return 1;
  }
  pg->haspreview = 1;
  uint32_t count = (uint32_t)pg->layout.cols * pg->layout.rows;
  pg->subs = malloc(sizeof(Tex3DS_SubTexture) * count);
  if(!pg->subs) {
    PGFX_ERR("Out of memory (subtextures)");
    return 1;
  }
  uint16_t tile = pg->layout.tile;
  for(uint32_t i = 0; i < count; i++) {
    uint16_t page, x, y;
    layout_tile(&pg->layout, i, &page, &x, &y);
    pg->subs[i] = texutil_subtex(x, y, tile, tile, LAYOUT_PAGESIZE, LAYOUT_PAGESIZE);
  }
  return 0;
}

static void puzzlegfx_setpreview(PuzzleGfx * pg, u16 w, u16 h) {
  pg->previewsub = texutil_subtex(0, 0, w, h, PGFX_PREVIEW_TEXW, PGFX_PREVIEW_TEXH);
}

static void puzzlegfx_srcinfo(const char * path, u64 * size, u64 * mtime) {
  struct stat st;
  *size = stat(path, &st) == 0 ? (u64)st.st_size : 0;
  *mtime = 0;
  archive_getmtime(path, mtime);
}

static int puzzlegfx_readcache(PuzzleGfx * pg, PieceGeo * geo, const char * cachepath,
    const PuzzleCacheHeader * want, char * err, size_t errlen) {
  FILE * f = fopen(cachepath, "rb");
  if(!f) return 1;
  PuzzleCacheHeader h;
  uint8_t * gbuf = NULL;
  int result = 1;
  if(fread(&h, sizeof(h), 1, f) != 1) goto done;
  if(memcmp(h.magic, want->magic, 4) || h.version != want->version || h.srcsize != want->srcsize ||
     h.srcmtime != want->srcmtime || h.target != want->target || h.seed != want->seed ||
     h.style != want->style) goto done;
  if(h.layout.pages > PGFX_MAXPAGES || h.geosize == 0 || h.geosize > PGFX_MAXGEO) goto done;
  pg->layout = h.layout;
  if(puzzlegfx_alloc(pg, err, errlen)) { result = 2; goto done; }
  for(int i = 0; i < pg->npages; i++) {
    if(fread(pg->pages[i].data, 1, pg->pages[i].size, f) != pg->pages[i].size) goto done;
    C3D_TexFlush(&pg->pages[i]);
  }
  if(fread(pg->preview.data, 1, pg->preview.size, f) != pg->preview.size) goto done;
  C3D_TexFlush(&pg->preview);
  puzzlegfx_setpreview(pg, h.previeww, h.previewh);
  gbuf = malloc(h.geosize);
  if(!gbuf) { PGFX_ERR("Out of memory (piece geometry)"); result = 2; goto done; }
  if(fread(gbuf, 1, h.geosize, f) != h.geosize || piecegeo_read(geo, gbuf, h.geosize)) goto done;
  // The textures have one tile per grid cell
  if(geo->count != (uint32_t)h.layout.cols * h.layout.rows) goto done;
  result = 0;
done:
  fclose(f);
  free(gbuf);
  if(result) {
    puzzlegfx_free(pg);
    piecegeo_free(geo);
  }
  return result;
}

static void puzzlegfx_writecache(PuzzleGfx * pg, const PieceGeo * geo, const char * cachepath, PuzzleCacheHeader * h) {
  // Cache failures are not fatal; we just rebuild next time
  h->geosize = piecegeo_size(geo);
  uint8_t * gbuf = malloc(h->geosize);
  if(!gbuf) return;
  piecegeo_write(geo, gbuf);
  char tmp[STORAGE_MAXTMP];
  storage_tmppath(cachepath, tmp);
  FILE * f = fopen(tmp, "wb");
  if(!f) {
    free(gbuf);
    return;
  }
  int ok = fwrite(h, sizeof(*h), 1, f) == 1;
  for(int i = 0; ok && i < pg->npages; i++) {
    ok = fwrite(pg->pages[i].data, 1, pg->pages[i].size, f) == pg->pages[i].size;
  }
  if(ok) ok = fwrite(pg->preview.data, 1, pg->preview.size, f) == pg->preview.size;
  if(ok) ok = fwrite(gbuf, 1, h->geosize, f) == h->geosize;
  free(gbuf);
  if(fclose(f) != 0) ok = 0;
  storage_commit(cachepath, ok);
}

static int puzzlegfx_build(PuzzleGfx * pg, PieceGeo * geo, const char * imagepath, PuzzleCacheHeader * h,
    progress_fn cb, void * udata, char * err, size_t errlen) {
  int result = 1;
  uint8_t * src = NULL, * img = NULL, * prev = NULL;
  uint32_t * tile = NULL;
  EdgeGen eg = {0};
  PieceBaker pb = {0};
  int sw, sh;

  if(cb) cb(udata, "Reading image", 0, 1);
  src = imageload_rgb(imagepath, &sw, &sh, err, errlen);
  if(!src) return 1;
  const EdgeStyle * style = edgegen_style(h->style);
  if(layout_compute(&pg->layout, sw, sh, h->target, PGFX_MAXSIDE, PGFX_MAXPAGES, style->reach)) {
    PGFX_ERR("Image too small for %lu pieces", (unsigned long)h->target);
    goto done;
  }
  Layout * l = &pg->layout;
  if(cb) cb(udata, "Scaling image", 0, 1);
  img = malloc((size_t)l->width * l->height * 3);
  if(!img) { PGFX_ERR("Out of memory (image)"); goto done; }
  if(imgscale_area(src, sw, sh, 3, l->cropx, l->cropy, l->cropw, l->croph, img, l->width, l->height)) {
    PGFX_ERR("Out of memory (scaling)");
    goto done;
  }
  imageload_free(src);
  src = NULL;

  if(puzzlegfx_alloc(pg, err, errlen)) goto done;
  for(int i = 0; i < pg->npages; i++) memset(pg->pages[i].data, 0, pg->pages[i].size);
  memset(pg->preview.data, 0, pg->preview.size);

  if(edgegen_init(&eg, l->cols, l->rows, h->seed, style) || piecebake_init(&pb, l->tile) ||
     piecegeo_grid(geo, l->cols, l->rows, l->cell, eg.corners)) {
    PGFX_ERR("Out of memory (cutter)");
    goto done;
  }
  tile = malloc(sizeof(uint32_t) * l->tile * l->tile);
  if(!tile) { PGFX_ERR("Out of memory (tile)"); goto done; }
  uint32_t count = (uint32_t)l->cols * l->rows;
  float outline[EDGEGEN_MAXPOINTS * 2];
  for(uint32_t i = 0; i < count; i++) {
    if(cb && (i % 8) == 0) cb(udata, "Cutting pieces", i, count);
    uint16_t col = i % l->cols, row = i / l->cols;
    int n = edgegen_outline(&eg, col, row, l->cell, l->margin, l->margin, outline);
    piecebake_tile(&pb, outline, n, img, l->width, l->height,
        (int32_t)col * l->cell - l->margin, (int32_t)row * l->cell - l->margin, tile);
    uint16_t page, x, y;
    layout_tile(l, i, &page, &x, &y);
    swizzle_copy(pg->pages[page].data, LAYOUT_PAGESIZE, x, y, tile, l->tile, l->tile, l->tile);
  }
  for(int i = 0; i < pg->npages; i++) C3D_TexFlush(&pg->pages[i]);

  // Preview: fit in the top screen, never upscale
  u16 pw, ph;
  imgscale_fit(l->width, l->height, PGFX_PREVIEW_MAXW, PGFX_PREVIEW_MAXH, &pw, &ph);
  prev = malloc((size_t)pw * ph * 3);
  if(!prev || imgscale_area(img, l->width, l->height, 3, 0, 0, l->width, l->height, prev, pw, ph)) {
    PGFX_ERR("Out of memory (preview)");
    goto done;
  }
  swizzle_copy_rgb(pg->preview.data, PGFX_PREVIEW_TEXW, 0, 0, prev, pw, ph);
  C3D_TexFlush(&pg->preview);
  puzzlegfx_setpreview(pg, pw, ph);
  h->layout = pg->layout;
  h->previeww = pw;
  h->previewh = ph;
  result = 0;
done:
  if(src) imageload_free(src);
  free(img);
  free(prev);
  free(tile);
  edgegen_free(&eg);
  piecebake_free(&pb);
  if(result) {
    puzzlegfx_free(pg);
    piecegeo_free(geo);
  }
  return result;
}

int puzzlegfx_load(PuzzleGfx * pg, PieceGeo * geo, const char * imagepath, const char * cachepath,
    uint32_t target, uint32_t seed, uint8_t style, progress_fn cb, void * udata, char * err, size_t errlen) {
  puzzlegfx_zero(pg);
  memset(geo, 0, sizeof(PieceGeo));
  err[0] = 0;
  PuzzleCacheHeader h;
  memset(&h, 0, sizeof(h));
  memcpy(h.magic, "JGCA", 4);
  h.version = PGFX_CACHE_VERSION;
  h.target = target;
  h.seed = seed;
  h.style = style;
  puzzlegfx_srcinfo(imagepath, &h.srcsize, &h.srcmtime);
  if(h.srcsize == 0) {
    PGFX_ERR("Can't find image %s", imagepath);
    return 1;
  }
  if(cb) cb(udata, "Loading", 0, 1);
  int r = puzzlegfx_readcache(pg, geo, cachepath, &h, err, errlen);
  if(r == 0) return 0;
  if(r == 2) return 1; // Out of memory; rebuilding won't help
  if(puzzlegfx_build(pg, geo, imagepath, &h, cb, udata, err, errlen)) return 1;
  if(cb) cb(udata, "Saving cache", 0, 1);
  puzzlegfx_writecache(pg, geo, cachepath, &h);
  return 0;
}

C2D_Image puzzlegfx_piece(PuzzleGfx * pg, uint32_t piece) {
  uint16_t page, x, y;
  layout_tile(&pg->layout, piece, &page, &x, &y);
  return (C2D_Image) { .tex = &pg->pages[page], .subtex = &pg->subs[piece] };
}

C2D_Image puzzlegfx_preview(PuzzleGfx * pg) {
  return (C2D_Image) { .tex = &pg->preview, .subtex = &pg->previewsub };
}

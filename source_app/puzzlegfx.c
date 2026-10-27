#include "puzzlegfx.h"
#include "cutter.h"
#include "imageload.h"
#include "imgscale.h"
#include "overlay.h"
#include "piecebake.h"
#include "storage.h"
#include "swizzle.h"
#include "texutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Raw struct on disk; only ever read back by the same build of the same program. The
// struct sizes guard against an edit that changes the layout but forgets the version.
typedef struct {
  char magic[4];
  u32 version;
  u16 headsize, layoutsize, tilesize;
  u16 overlay;            // OVERLAY_* (was padding, zero: caches from before overlays are "none")
  u64 srcsize;
  u64 srcmtime;
  u32 target;
  u32 seed;
  u32 style;
  u32 whimsy, whimsyset;
  Layout layout;
  u16 previeww, previewh;
  u32 count;              // Pieces: a LayoutTile table of this many follows the header
  u32 cuthash;            // cutter_hash of the cut
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
  free(pg->tiles);
  free(pg->pageof);
  puzzlegfx_zero(pg);
}

// Textures and subtextures for pg->layout and pg->tiles (count of them)
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
  pg->subs = malloc(sizeof(Tex3DS_SubTexture) * pg->count);
  pg->pageof = malloc(pg->count);
  if(!pg->subs || !pg->pageof) {
    PGFX_ERR("Out of memory (subtextures)");
    return 1;
  }
  for(uint32_t i = 0; i < pg->count; i++) {
    const LayoutTile * t = pg->tiles + i;
    pg->subs[i] = texutil_subtex(t->x, t->y, t->w, t->h, LAYOUT_PAGESIZE, LAYOUT_PAGESIZE);
    pg->pageof[i] = t->page;
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
  if(memcmp(h.magic, want->magic, 4) || h.version != want->version || h.headsize != want->headsize ||
     h.layoutsize != want->layoutsize || h.tilesize != want->tilesize || h.srcsize != want->srcsize ||
     h.srcmtime != want->srcmtime || h.target != want->target || h.seed != want->seed ||
     h.style != want->style || h.whimsy != want->whimsy || h.whimsyset != want->whimsyset ||
     h.overlay != want->overlay) goto done;
  if(h.layout.pages > PGFX_MAXPAGES || h.geosize == 0 || h.geosize > PGFX_MAXGEO ||
     h.count == 0 || h.count > 0xFFF0) goto done;
  pg->layout = h.layout;
  pg->count = h.count;
  pg->tiles = malloc(sizeof(LayoutTile) * h.count);
  if(!pg->tiles) { PGFX_ERR("Out of memory (tiles)"); result = 2; goto done; }
  if(fread(pg->tiles, sizeof(LayoutTile), h.count, f) != h.count) goto done;
  // The layout must agree with itself and with the tiles (a damaged file that still has a
  // good header shouldn't give odd sizes)
  const Layout * l = &h.layout;
  if(l->pages == 0 || l->cols == 0 || l->rows == 0 || l->cell < LAYOUT_MINCELL ||
     l->width != (uint32_t)l->cols * l->cell || l->height != (uint32_t)l->rows * l->cell) goto done;
  uint16_t maxw = 0, maxh = 0;
  for(uint32_t i = 0; i < h.count; i++) {
    const LayoutTile * t = pg->tiles + i;
    if(t->page >= l->pages || t->w == 0 || t->h == 0 || t->x + t->w > LAYOUT_PAGESIZE ||
       t->y + t->h > LAYOUT_PAGESIZE) goto done;
    if(t->w > maxw) maxw = t->w;
    if(t->h > maxh) maxh = t->h;
  }
  if(maxw != l->maxw || maxh != l->maxh) goto done;
  pg->cuthash = h.cuthash;
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
  if(geo->count != h.count || geo->cols != l->cols || geo->rows != l->rows || geo->cell != l->cell ||
     geo->width != l->width || geo->height != l->height) goto done;
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
  if(ok) ok = fwrite(pg->tiles, sizeof(LayoutTile), pg->count, f) == pg->count;
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
  float * boxes = NULL, * outline = NULL;
  Cutter cut;
  int hascut = 0;
  PieceBaker pb = {0};
  int sw, sh;

  if(cb) cb(udata, "Reading image", 0, 1);
  src = imageload_rgb(imagepath, &sw, &sh, err, errlen);
  if(!src) return 1;
  Layout * l = &pg->layout;
  if(layout_crop(l, sw, sh, h->target)) {
    PGFX_ERR("Image too small for %lu pieces", (unsigned long)h->target);
    goto done;
  }
  // Cut first (in cells), then pick the piece size whose tiles fit the pages
  if(cb) cb(udata, "Cutting pieces", 0, 1);
  if(cutter_init(&cut, h->style, h->whimsy, h->whimsyset, l->cols, l->rows, h->seed)) {
    PGFX_ERR("Out of memory (cutter)");
    goto done;
  }
  hascut = 1;
  pg->count = cut.count;
  pg->cuthash = cutter_hash(&cut);
  boxes = malloc(sizeof(float) * 4 * cut.count);
  pg->tiles = malloc(sizeof(LayoutTile) * cut.count);
  outline = malloc(sizeof(float) * 2 * cut.maxpoints);
  if(!boxes || !pg->tiles || !outline || cutter_bounds(&cut, boxes)) {
    PGFX_ERR("Out of memory (tiles)");
    goto done;
  }
  int r = layout_fit(l, boxes, cut.count, PGFX_MAXSIDE, PGFX_MAXPAGES, pg->tiles);
  if(r) {
    if(r == 1) PGFX_ERR("Out of memory (tiles)");
    else PGFX_ERR("Image too small for %lu pieces", (unsigned long)h->target);
    goto done;
  }
  if(cb) cb(udata, "Scaling image", 0, 1);
  img = malloc((size_t)l->width * l->height * 3);
  if(!img) { PGFX_ERR("Out of memory (image)"); goto done; }
  if(imgscale_area(src, sw, sh, 3, l->cropx, l->cropy, l->cropw, l->croph, img, l->width, l->height)) {
    PGFX_ERR("Out of memory (scaling)");
    goto done;
  }
  imageload_free(src);
  src = NULL;
  if(h->overlay) {
    if(cb) cb(udata, "Coloring image", 0, 1);
    overlay_apply(h->overlay, img, l->width, l->height);
  }

  if(puzzlegfx_alloc(pg, err, errlen)) goto done;
  for(int i = 0; i < pg->npages; i++) memset(pg->pages[i].data, 0, pg->pages[i].size);
  memset(pg->preview.data, 0, pg->preview.size);
  // Geometry at this cell size; each piece's home center is its tile's center
  if(cutter_geo(&cut, l->cell, geo) || piecebake_init(&pb, l->maxw, l->maxh)) {
    PGFX_ERR("Out of memory (cutter)");
    goto done;
  }
  for(uint32_t i = 0; i < cut.count; i++) {
    geo->center[i * 2] = pg->tiles[i].ox + pg->tiles[i].w / 2.0f;
    geo->center[i * 2 + 1] = pg->tiles[i].oy + pg->tiles[i].h / 2.0f;
  }
  tile = malloc(sizeof(uint32_t) * l->maxw * l->maxh);
  if(!tile) { PGFX_ERR("Out of memory (tile)"); goto done; }
  for(uint32_t i = 0; i < cut.count; i++) {
    if(cb && (i % 8) == 0) cb(udata, "Drawing pieces", i, cut.count);
    const LayoutTile * t = pg->tiles + i;
    uint32_t n = cutter_outline(&cut, i, l->cell, -t->ox, -t->oy, outline);
    piecebake_tile(&pb, outline, n, img, l->width, l->height, t->ox, t->oy, t->w, t->h, tile);
    swizzle_copy(pg->pages[t->page].data, LAYOUT_PAGESIZE, t->x, t->y, tile, t->w, t->h, t->w);
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
  h->count = pg->count;
  h->cuthash = pg->cuthash;
  result = 0;
done:
  if(src) imageload_free(src);
  free(img);
  free(prev);
  free(tile);
  free(boxes);
  free(outline);
  if(hascut) cutter_free(&cut);
  piecebake_free(&pb);
  if(result) {
    puzzlegfx_free(pg);
    piecegeo_free(geo);
  }
  return result;
}

int puzzlegfx_load(PuzzleGfx * pg, PieceGeo * geo, const char * imagepath, const char * cachepath,
    uint32_t target, uint32_t seed, uint8_t style, uint8_t whimsy, uint8_t whimsyset, uint8_t overlay,
    progress_fn cb, void * udata, char * err, size_t errlen) {
  puzzlegfx_zero(pg);
  memset(geo, 0, sizeof(PieceGeo));
  err[0] = 0;
  PuzzleCacheHeader h;
  memset(&h, 0, sizeof(h));
  memcpy(h.magic, "JGCA", 4);
  h.version = PGFX_CACHE_VERSION;
  h.headsize = sizeof(PuzzleCacheHeader);
  h.layoutsize = sizeof(Layout);
  h.tilesize = sizeof(LayoutTile);
  h.target = target;
  h.seed = seed;
  h.style = style;
  h.whimsy = whimsy;
  h.whimsyset = whimsyset;
  h.overlay = overlay;
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
  return (C2D_Image) { .tex = &pg->pages[pg->tiles[piece].page], .subtex = &pg->subs[piece] };
}

C2D_Image puzzlegfx_preview(PuzzleGfx * pg) {
  return (C2D_Image) { .tex = &pg->preview, .subtex = &pg->previewsub };
}

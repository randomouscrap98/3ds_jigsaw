// Cut preview: renders cuts through the cutter (any style, whimsies) so they can be tuned
// by eye.
//   make -f Makefile.host build_host/cut_preview
//   ./build_host/cut_preview OUTDIR [image.jpg] [seed]
// Writes OUTDIR/shapes.png (the whimsy shapes), OUTDIR/<style>_<whimsy>_<count>_cut.png
// (solved, whimsies tinted) and OUTDIR/<style>_<whimsy>_<count>_pieces.png (the image on
// the pieces, pulled apart), OUTDIR/sample_<style>_<whimsy>.png (the menu's cut sample over
// the picture at thumbnail size), plus stats on stdout.
#include "cutter.h"
#include "piecebake.h"
#include "imgscale.h"
#include "layout.h"
#include "cutsample.h"
#include "swizzle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define OUTW 1100     // Rough output width (pixels)
#define GAP 0.08f     // Pulled-apart spacing (fraction of a piece position)

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

// Bake every piece from img (W x H) and draw it at its home spot scaled out by spread.
// Whimsies (index >= first) get tinted when tint is set.
static void render(const Cutter * c, float cell, const uint8_t * img, uint32_t W, uint32_t H,
    float spread, int tint, const char * path) {
  float * boxes = malloc(sizeof(float) * 4 * c->count);
  float * xy = malloc(sizeof(float) * 2 * c->maxpoints);
  cutter_bounds(c, boxes);
  float side = 0;
  for(uint32_t p = 0; p < c->count; p++) {
    side = fmaxf(side, (boxes[4 * p + 2] - boxes[4 * p]) * cell);
    side = fmaxf(side, (boxes[4 * p + 3] - boxes[4 * p + 1]) * cell);
  }
  uint16_t tile = (uint16_t)side + 6;
  uint32_t CW = (uint32_t)(W * (1 + spread)) + tile, CH = (uint32_t)(H * (1 + spread)) + tile;
  uint8_t * canvas = malloc(CW * CH * 3);
  memset(canvas, 235, CW * CH * 3);
  uint32_t * out = malloc(sizeof(uint32_t) * tile * tile);
  PieceBaker pb;
  piecebake_init(&pb, tile, tile);
  uint32_t first = (uint32_t)c->cols * c->rows;
  for(uint32_t p = 0; p < c->count; p++) {
    float cx = (boxes[4 * p] + boxes[4 * p + 2]) / 2 * cell, cy = (boxes[4 * p + 1] + boxes[4 * p + 3]) / 2 * cell;
    int32_t ox = (int32_t)floorf(cx - tile / 2.0f), oy = (int32_t)floorf(cy - tile / 2.0f);
    uint32_t n = cutter_outline(c, p, cell, -ox, -oy, xy);
    piecebake_tile(&pb, xy, n, img, W, H, ox, oy, tile, tile, out);
    int32_t dx = (int32_t)(cx * spread) + ox + tile / 2, dy = (int32_t)(cy * spread) + oy + tile / 2;
    int whim = tint && p >= first;
    for(int y = 0; y < tile; y++) for(int x = 0; x < tile; x++) {
      int32_t X = dx + x, Y = dy + y;
      if(X < 0 || Y < 0 || X >= (int32_t)CW || Y >= (int32_t)CH) continue;
      uint32_t px = out[y * tile + x];
      float al = (px & 0xFF) / 255.0f;
      float r = px >> 24, g = (px >> 16) & 0xFF, b = (px >> 8) & 0xFF;
      if(whim) { r = r * 0.6f + 255 * 0.4f; g = g * 0.6f + 120 * 0.4f; b = b * 0.6f + 160 * 0.4f; }
      uint8_t * d = canvas + (Y * CW + X) * 3;
      d[0] = d[0] * (1 - al) + r * al;
      d[1] = d[1] * (1 - al) + g * al;
      d[2] = d[2] * (1 - al) + b * al;
    }
  }
  stbi_write_png(path, CW, CH, 3, canvas, CW * 3);
  piecebake_free(&pb);
  free(out);
  free(canvas);
  free(xy);
  free(boxes);
}

typedef struct { float r, sp; uint32_t p; } Thin;
// Worst first: most spike area
static int thincmp(const void * a, const void * b) {
  float x = ((const Thin *)a)->sp, y = ((const Thin *)b)->sp;
  return x > y ? -1 : x < y;
}

// The spikiest pieces next to whimsies (most area in parts thinner than 2 * WHIMSY_SPIKER),
// each on its own (whimsy neighbours faint pink)
static void thinsheet(const Cutter * c, const char * path, const char * name) {
  const CutGraph * g = &c->g;
  float * body = malloc(sizeof(float) * 2 * c->maxpoints);
  Thin * list = malloc(sizeof(Thin) * g->count);
  uint32_t nl = 0;
  float normal = 0;
  uint32_t nnormal = 0;
  for(uint32_t p = 0; p < g->count; p++) {
    if(cutgraph_iswhimsy(g, p)) continue;
    int near = 0;
    for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) near |= (g->e[g->pedge[k] >> 1].flags & CUTEDGE_WHIMSY) != 0;
    uint32_t n = cutgraph_body(g, p, 1, 0, 0, body);
    float r, sp;
    whimsy_measure(body, n, &r, &sp);
    if(near) list[nl++] = (Thin) { r, sp, p };
    else if(nnormal < 60) { normal += r; nnormal++; }
  }
  qsort(list, nl, sizeof(Thin), thincmp);
  float thinnest = 1e9f;
  for(uint32_t i = 0; i < nl; i++) thinnest = fminf(thinnest, list[i].r);
  printf("  %s: %u pieces next to whimsies, thinnest %.2f (ordinary ~%.2f); most spike area:", name, nl, thinnest, normal / nnormal);
  uint32_t show = nl < 10 ? nl : 10, S = 300;
  const float scale = 100;
  uint32_t W = S * show, H = S;
  uint8_t * canvas = malloc(W * H * 3);
  memset(canvas, 235, W * H * 3);
  uint8_t * grey = malloc(S * S * 3), * pink = malloc(S * S * 3);
  memset(grey, 200, S * S * 3);
  for(uint32_t i = 0; i < S * S; i++) { pink[3 * i] = 245; pink[3 * i + 1] = 205; pink[3 * i + 2] = 215; }
  PieceBaker pb;
  piecebake_init(&pb, S, S);
  uint32_t * out = malloc(sizeof(uint32_t) * S * S);
  float * xy = malloc(sizeof(float) * 2 * c->maxpoints);
  for(uint32_t i = 0; i < show; i++) {
    uint32_t p = list[i].p;
    printf(" %.3f", list[i].sp);
    uint32_t n = cutter_outline(c, p, scale, 0, 0, xy);
    float cx = 0, cy = 0;
    for(uint32_t k = 0; k < n; k++) { cx += xy[2 * k]; cy += xy[2 * k + 1]; }
    cx /= n; cy /= n;
    float ox = S / 2.0f - cx, oy = S / 2.0f - cy;
    // Whimsy neighbours first, faint
    for(uint32_t k = g->pstart[p]; k < g->pstart[p + 1]; k++) {
      const CutEdge * e = g->e + (g->pedge[k] >> 1);
      if(!(e->flags & CUTEDGE_WHIMSY)) continue;
      uint32_t w = e->left == (int32_t)p ? e->right : e->left;
      uint32_t m = cutter_outline(c, w, scale, ox, oy, xy);
      piecebake_tile(&pb, xy, m, pink, S, S, 0, 0, S, S, out);
      for(uint32_t y = 0; y < S; y++) for(uint32_t x = 0; x < S; x++) {
        uint32_t px = out[y * S + x];
        float al = (px & 0xFF) / 255.0f * 0.6f;
        uint8_t * d = canvas + (y * W + i * S + x) * 3;
        for(int q = 0; q < 3; q++) d[q] = d[q] * (1 - al) + ((px >> (24 - 8 * q)) & 0xFF) * al;
      }
    }
    n = cutter_outline(c, p, scale, ox, oy, xy);
    piecebake_tile(&pb, xy, n, grey, S, S, 0, 0, S, S, out);
    for(uint32_t y = 0; y < S; y++) for(uint32_t x = 0; x < S; x++) {
      uint32_t px = out[y * S + x];
      float al = (px & 0xFF) / 255.0f;
      uint8_t * d = canvas + (y * W + i * S + x) * 3;
      for(int q = 0; q < 3; q++) d[q] = d[q] * (1 - al) + ((px >> (24 - 8 * q)) & 0xFF) * al;
    }
  }
  printf("\n");
  stbi_write_png(path, W, H, 3, canvas, W * 3);
  piecebake_free(&pb);
  free(out); free(grey); free(pink); free(canvas); free(xy); free(body); free(list);
}

// The set's shapes side by side
static void shapes(const char * dir) {
  uint32_t ns = whimsy_setsize(WHIMSY_SET_CUTE), S = 220;
  uint32_t W = S * ns, H = S;
  uint8_t * canvas = malloc(W * H * 3);
  memset(canvas, 235, W * H * 3);
  uint8_t * img = malloc(S * S * 3);
  memset(img, 200, S * S * 3);
  PieceBaker pb;
  piecebake_init(&pb, S, S);
  uint32_t * out = malloc(sizeof(uint32_t) * S * S);
  float xy[WHIMSY_MAXSHAPE * 2];
  for(uint32_t i = 0; i < ns; i++) {
    uint32_t n = whimsy_shape(WHIMSY_SET_CUTE, i, xy);
    for(uint32_t k = 0; k < n; k++) { xy[2 * k] = S / 2 + xy[2 * k] * S * 0.33f; xy[2 * k + 1] = S / 2 + xy[2 * k + 1] * S * 0.33f; }
    piecebake_tile(&pb, xy, n, img, S, S, 0, 0, S, S, out);
    for(uint32_t y = 0; y < S; y++) for(uint32_t x = 0; x < S; x++) {
      uint32_t px = out[y * S + x];
      float al = (px & 0xFF) / 255.0f;
      uint8_t * d = canvas + (y * W + i * S + x) * 3;
      for(int k = 0; k < 3; k++) d[k] = d[k] * (1 - al) + ((px >> (24 - 8 * k)) & 0xFF) * al;
    }
  }
  char path[1024];
  snprintf(path, sizeof path, "%s/shapes.png", dir);
  stbi_write_png(path, W, H, 3, canvas, W * 3);
  piecebake_free(&pb);
  free(out); free(img); free(canvas);
}

int main(int argc, char ** argv) {
  if(argc < 2) {
    fprintf(stderr, "usage: %s OUTDIR [image] [seed]\n", argv[0]);
    return 1;
  }
  const char * imgpath = argc > 2 ? argv[2] : "romfs/puzzles/The Gulf Stream.jpg";
  uint32_t seed = argc > 3 ? strtoul(argv[3], NULL, 0) : 12345;
  int sw, sh, comp;
  uint8_t * src = stbi_load(imgpath, &sw, &sh, &comp, 3);
  if(!src) {
    fprintf(stderr, "can't load %s\n", imgpath);
    return 1;
  }
  shapes(argv[1]);
  const char * stylenames[CUT_STYLES] = { "classic", "irregular", "wild", "random", "randrect" };
  const char * whimnames[WHIMSY_LEVELS] = { "none", "few", "many" };
  const uint8_t styles[] = { CUT_STYLE_CLASSIC, CUT_STYLE_WILD, CUT_STYLE_RANDOM, CUT_STYLE_RANDRECT };
  const uint32_t targets[] = { 100, 500 };
  for(int si = 0; si < (int)sizeof styles; si++) {
    for(uint8_t wh = WHIMSY_FEW; wh <= WHIMSY_MANY; wh++) {
      for(int t = 0; t < 2; t++) {
        uint16_t cols, rows;
        layout_grid(sw, sh, targets[t], &cols, &rows);
        Cutter c;
        double t0 = now_ms();
        if(cutter_init(&c, styles[si], wh, WHIMSY_SET_CUTE, cols, rows, seed)) {
          printf("%s %s %u: FAILED\n", stylenames[styles[si]], whimnames[wh], targets[t]);
          continue;
        }
        double ms = now_ms() - t0;
        printf("%s %s %u (%ux%u): %.1f ms, whimsies %u of %u, tries %u, tab passes %u\n",
            stylenames[styles[si]], whimnames[wh], targets[t], cols, rows, ms, c.whimsies,
            whimsy_count(wh, cols * rows), c.g.tries, c.g.passes);
        float cell = (float)OUTW / cols;
        uint32_t W = (uint32_t)(cols * cell), H = (uint32_t)(rows * cell);
        uint8_t * img = malloc(W * H * 3);
        imgscale_area(src, sw, sh, 3, 0, 0, sw, sh, img, W, H);
        uint8_t * grey = malloc(W * H * 3);
        memset(grey, 200, W * H * 3);
        char path[1024];
        snprintf(path, sizeof path, "%s/%s_%s_%u_cut.png", argv[1], stylenames[styles[si]], whimnames[wh], targets[t]);
        render(&c, cell, grey, W, H, 0, 1, path);
        snprintf(path, sizeof path, "%s/%s_%s_%u_pieces.png", argv[1], stylenames[styles[si]], whimnames[wh], targets[t]);
        render(&c, cell, img, W, H, GAP, 0, path);
        if(wh == WHIMSY_MANY) {
          char name[64];
          snprintf(name, sizeof name, "%s_%u", stylenames[styles[si]], targets[t]);
          snprintf(path, sizeof path, "%s/spiky_%s.png", argv[1], name);
          thinsheet(&c, path, name);
        }
        free(img);
        free(grey);
        cutter_free(&c);
      }
    }
  }
  // The menu's cut sample over the picture, at thumbnail size (thumb.h: fit 320 x 200)
  uint16_t tw, th;
  imgscale_fit(sw, sh, 320, 200, &tw, &th);
  uint8_t * thumb = malloc((size_t)tw * th * 3);
  uint32_t * cut = malloc(sizeof(uint32_t) * tw * th);
  uint8_t * out = malloc((size_t)tw * th * 3);
  imgscale_area(src, sw, sh, 3, 0, 0, sw, sh, thumb, tw, th);
  for(uint8_t st = 0; st < CUT_STYLES; st++) {
    for(uint8_t wh = 0; wh < WHIMSY_LEVELS; wh++) {
      double t0 = now_ms();
      if(cutsample_draw(st, wh, WHIMSY_SET_CUTE, tw, th, cut)) {
        printf("sample %s %s: FAILED\n", stylenames[st], whimnames[wh]);
        continue;
      }
      printf("sample %s %s (%ux%u): %.1f ms\n", stylenames[st], whimnames[wh], tw, th, now_ms() - t0);
      for(size_t i = 0; i < (size_t)tw * th; i++) {
        uint32_t p = cut[i];
        float a = (p & 0xFF) / 255.0f;
        for(int k = 0; k < 3; k++) {
          float c = (p >> (24 - 8 * k)) & 0xFF;
          out[3 * i + k] = (uint8_t)(thumb[3 * i + k] * (1 - a) + c * a + 0.5f);
        }
      }
      char path[1024];
      snprintf(path, sizeof path, "%s/sample_%s_%s.png", argv[1], stylenames[st], whimnames[wh]);
      stbi_write_png(path, tw, th, 3, out, tw * 3);
    }
  }
  free(thumb);
  free(cut);
  free(out);
  stbi_image_free(src);
  return 0;
}

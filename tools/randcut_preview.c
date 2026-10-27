// Random cut preview: renders cuts for every random style at several piece counts, plus
// stats, so the generator can be tuned by eye.
//   make -f Makefile.host build_host/randcut_preview
//   ./build_host/randcut_preview OUTDIR [image.jpg] [seed]
// Writes OUTDIR/<style>_<count>_cut.png (solved, outlines on grey) and
// OUTDIR/<style>_<count>_pieces.png (image baked onto the pieces, pulled apart).
#include "randcut.h"
#include "piecebake.h"
#include "imgscale.h"
#include "layout.h"

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

static float polyarea(const float * xy, uint32_t n) {
  double a = 0;
  for(uint32_t i = 0; i < n; i++) {
    uint32_t j = (i + 1) % n;
    a += (double)xy[i * 2] * xy[j * 2 + 1] - (double)xy[j * 2] * xy[i * 2 + 1];
  }
  return a / 2;
}

// Bake every piece from img (W x H) and draw it at its home spot scaled out by spread
static void render(const RandCut * c, const PieceGeo * g, float cell, const uint8_t * img,
    uint32_t W, uint32_t H, float spread, const char * path) {
  uint32_t maxp = randcut_maxpoints(c);
  float * xy = malloc(sizeof(float) * 2 * maxp);
  // Tile = biggest piece bounding box
  float side = 0;
  for(uint32_t p = 0; p < c->count; p++) {
    uint32_t n = randcut_outline(c, p, cell, 0, 0, xy);
    for(uint32_t i = 0; i < n; i++) {
      side = fmaxf(side, 2 * fabsf(xy[i * 2] - g->center[p * 2]));
      side = fmaxf(side, 2 * fabsf(xy[i * 2 + 1] - g->center[p * 2 + 1]));
    }
  }
  uint16_t tile = (uint16_t)side + 4;
  uint32_t CW = (uint32_t)(W * (1 + spread)) + tile, CH = (uint32_t)(H * (1 + spread)) + tile;
  uint8_t * canvas = malloc(CW * CH * 3);
  memset(canvas, 235, CW * CH * 3);
  uint32_t * out = malloc(sizeof(uint32_t) * tile * tile);
  PieceBaker pb;
  piecebake_init(&pb, tile, tile);
  for(uint32_t p = 0; p < c->count; p++) {
    float cx = g->center[p * 2], cy = g->center[p * 2 + 1];
    int32_t ox = (int32_t)floorf(cx - tile / 2.0f), oy = (int32_t)floorf(cy - tile / 2.0f);
    uint32_t n = randcut_outline(c, p, cell, -ox, -oy, xy);
    piecebake_tile(&pb, xy, n, img, W, H, ox, oy, tile, tile, out);
    int32_t dx = (int32_t)(cx * (1 + spread)) - (cx - ox) + tile / 2, dy = (int32_t)(cy * (1 + spread)) - (cy - oy) + tile / 2;
    for(int y = 0; y < tile; y++) for(int x = 0; x < tile; x++) {
      int32_t X = dx + x, Y = dy + y;
      if(X < 0 || Y < 0 || X >= (int32_t)CW || Y >= (int32_t)CH) continue;
      uint32_t px = out[y * tile + x];
      float al = (px & 0xFF) / 255.0f;
      uint8_t * d = canvas + (Y * CW + X) * 3;
      d[0] = d[0] * (1 - al) + (px >> 24) * al;
      d[1] = d[1] * (1 - al) + ((px >> 16) & 0xFF) * al;
      d[2] = d[2] * (1 - al) + ((px >> 8) & 0xFF) * al;
    }
  }
  stbi_write_png(path, CW, CH, 3, canvas, CW * 3);
  piecebake_free(&pb);
  free(out);
  free(canvas);
  free(xy);
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
  const char * names[RANDCUT_STYLES] = { "random" };
  const uint32_t targets[] = { 100, 250, 500 };
  for(int st = 0; st < RANDCUT_STYLES; st++) {
    for(int t = 0; t < 3; t++) {
      uint16_t cols, rows;
      layout_grid(sw, sh, targets[t], &cols, &rows);
      uint32_t count = cols * rows;
      RandCut c;
      double t0 = now_ms();
      if(randcut_init(&c, cols, rows, count, seed, randcut_style(st))) {
        printf("%s %u: FAILED after %u tries\n", names[st], count, c.tries);
        continue;
      }
      double ms = now_ms() - t0;
      float cell = (float)OUTW / cols;
      uint32_t W = (uint32_t)(cols * cell), H = (uint32_t)(rows * cell);
      PieceGeo g;
      randcut_geo(&c, cell, &g);
      // Stats: areas (cells^2), sides, tabs per piece, tabs per edge
      float amin = 1e9f, amax = 0;
      double asum = 0, asq = 0;
      uint32_t sides[16] = {0}, ptabs[16] = {0}, etabs[3] = {0};
      for(uint32_t p = 0; p < count; p++) {
        uint32_t n = g.hbstart[p + 1] - g.hbstart[p];
        float a = polyarea(g.hb + 2 * g.hbstart[p], n) / (cell * cell);
        amin = fminf(amin, a); amax = fmaxf(amax, a);
        asum += a; asq += a * a;
        sides[n < 15 ? n : 15]++;
        int tabs = 0;
        for(uint32_t k = c.pstart[p]; k < c.pstart[p + 1]; k++) tabs += c.e[c.pedge[k] >> 1].ntabs;
        ptabs[tabs < 15 ? tabs : 15]++;
      }
      for(uint32_t i = 0; i < c.ne; i++) if(c.e[i].left >= 0 && c.e[i].right >= 0) etabs[c.e[i].ntabs]++;
      double mean = asum / count;
      printf("%s %u pieces (%ux%u): %.1f ms, tries %u, tab passes %u\n", names[st], count, cols, rows, ms, c.tries, c.passes);
      printf("  area: min %.2f max %.2f stddev %.2f\n", amin, amax, sqrt(asq / count - mean * mean));
      printf("  sides:");
      for(int i = 3; i < 16; i++) if(sides[i]) printf(" %d:%u", i, sides[i]);
      printf("\n  tabs per piece:");
      for(int i = 0; i < 16; i++) if(ptabs[i]) printf(" %d:%u", i, ptabs[i]);
      printf("\n  inner edges by tabs: 0:%u 1:%u 2:%u\n", etabs[0], etabs[1], etabs[2]);
      // Image at puzzle size
      uint8_t * img = malloc(W * H * 3);
      imgscale_area(src, sw, sh, 3, 0, 0, sw, sh, img, W, H);
      uint8_t * grey = malloc(W * H * 3);
      memset(grey, 200, W * H * 3);
      char path[1024];
      snprintf(path, sizeof path, "%s/%s_%u_cut.png", argv[1], names[st], targets[t]);
      render(&c, &g, cell, grey, W, H, 0, path);
      snprintf(path, sizeof path, "%s/%s_%u_pieces.png", argv[1], names[st], targets[t]);
      render(&c, &g, cell, img, W, H, GAP, path);
      free(img);
      free(grey);
      piecegeo_free(&g);
      randcut_free(&c);
    }
  }
  stbi_image_free(src);
  return 0;
}

// Tessellation cut preview: tess_preview OUT.png COLS ROWS SEED [JITTER BUMP CLEAR [CELLPX]]
// Pieces filled by orientation (red / yellow / green, lightness varied per piece), dark
// outlines; prints TessStats. JITTER 0 BUMP 0 = the plain tiling. TESS_SHAPE=n: TESSCUT_SHAPE_*.
// TESS_QUILT=1: each piece a random quilt colour instead (one orientation: Pinwheel).
#include "tesscut.h"
#include "geom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main(int argc, char ** argv) {
  if(argc < 5) {
    fprintf(stderr, "usage: %s OUT.png COLS ROWS SEED [JITTER BUMP CLEAR [CELLPX]]\n", argv[0]);
    return 1;
  }
  int cols = atoi(argv[2]), rows = atoi(argv[3]);
  uint32_t seed = strtoul(argv[4], NULL, 0);
  TessVary v = { 0.08f, 0.08f, 0.06f, 0.4f, 0.2f };
  if(argc > 7) {
    v.jitter = atof(argv[5]);
    v.bump = atof(argv[6]);
    v.clear = atof(argv[7]);
  }
  float cell = argc > 8 ? atof(argv[8]) : 40;
  if(getenv("TESS_NECK")) v.neck = atof(getenv("TESS_NECK"));
  CutGraph g;
  uint8_t * ori;
  TessStats st;
  int shape = getenv("TESS_SHAPE") ? atoi(getenv("TESS_SHAPE")) : TESSCUT_SHAPE_LIZARD;
  if(tesscut_graph(cols, rows, tesscut_shape(shape), &v, seed, &g, &ori, &st)) {
    fprintf(stderr, "cut failed\n");
    return 1;
  }
  printf("%u pieces (target %d), mismatched edges %u, thin before changes %u, passes %u, flattened %u, merged %u, fits %u, failed %u, necks %u (undone %u)\n",
      g.count, cols * rows, st.mismatched, st.thinbase, st.passes, st.flattened, st.merged, st.fits, st.failed, st.necks, st.neckundo);
  uint32_t maxp = cutgraph_maxpoints(&g);
  float * xy = malloc(sizeof(float) * 2 * maxp);
  // Distinctness: every piece against its most similar same-orientation piece, outlines
  // centered on their centroids: the biggest gap (Hausdorff, in cells). Ragged edge pieces
  // (no full ring of neighbours) are skipped.
  if(getenv("TESS_DISTINCT")) {
    enum { NS = 160 };
    float * sp = malloc(sizeof(float) * 2 * NS * g.count);
    uint8_t * inner = calloc(g.count, 1);
    for(uint32_t p = 0; p < g.count; p++) {
      inner[p] = 1;
      for(uint32_t q = g.pstart[p]; q < g.pstart[p + 1]; q++) {
        const CutEdge * e = g.e + g.pedge[q] / 2;
        if(e->left < 0 || e->right < 0) inner[p] = 0;
      }
      uint32_t n = cutgraph_outline(&g, p, 1, 0, 0, xy);
      float cx, cy;
      geom_area(xy, n, &cx, &cy);
      float tot = 0;
      for(uint32_t i = 0; i < n; i++) { uint32_t j = (i + 1) % n; tot += hypotf(xy[2 * j] - xy[2 * i], xy[2 * j + 1] - xy[2 * i + 1]); }
      uint32_t i = 0;
      float acc = 0;
      for(int k = 0; k < NS; k++) {
        float want = tot * k / NS;
        for(;;) {
          uint32_t j = (i + 1) % n;
          float l = hypotf(xy[2 * j] - xy[2 * i], xy[2 * j + 1] - xy[2 * i + 1]);
          if(acc + l >= want || i + 1 >= n) {
            float u = l > 0 ? (want - acc) / l : 0;
            sp[2 * (p * NS + k)] = xy[2 * i] + (xy[2 * j] - xy[2 * i]) * u - cx;
            sp[2 * (p * NS + k) + 1] = xy[2 * i + 1] + (xy[2 * j + 1] - xy[2 * i + 1]) * u - cy;
            break;
          }
          acc += l;
          i++;
        }
      }
    }
    float * best = malloc(sizeof(float) * g.count);
    uint32_t ni = 0;
    for(uint32_t p = 0; p < g.count; p++) {
      if(!inner[p]) continue;
      best[ni] = 1e9f;
      for(uint32_t q = 0; q < g.count; q++) {
        if(q == p || !inner[q] || (ori[q] != ori[p] && atoi(getenv("TESS_DISTINCT")) != 2)) continue;   // 2: every piece (shapes that look alike turned)
        float h = 0;
        for(int dir = 0; dir < 2; dir++) {
          const float * A = sp + 2 * NS * (dir ? q : p), * B = sp + 2 * NS * (dir ? p : q);
          for(int a = 0; a < NS && h < best[ni]; a++) {
            float dmin = 1e9f;
            for(int b = 0; b < NS; b++) {
              float dx = A[2 * a] - B[2 * b], dy = A[2 * a + 1] - B[2 * b + 1];
              dmin = fminf(dmin, dx * dx + dy * dy);
            }
            h = fmaxf(h, dmin);
          }
        }
        best[ni] = fminf(best[ni], h);
      }
      best[ni] = sqrtf(best[ni]);
      ni++;
    }
    // Sort for percentiles
    for(uint32_t a = 1; a < ni; a++) { float t = best[a]; uint32_t b = a; for(; b > 0 && best[b - 1] > t; b--) best[b] = best[b - 1]; best[b] = t; }
    printf("distinct (most similar same-orientation piece, biggest gap, cells): min %.3f  p10 %.3f  median %.3f  (%u inner pieces)\n",
        best[0], best[ni / 10], best[ni / 2], ni);
    printf("  in px at 500 pieces (39 px cell): min %.1f p10 %.1f median %.1f; at 100 (85 px): min %.1f p10 %.1f median %.1f\n",
        best[0] * 39, best[ni / 10] * 39, best[ni / 2] * 39, best[0] * 85, best[ni / 10] * 85, best[ni / 2] * 85);
    free(best); free(sp); free(inner);
  }
  // Final pieces: smallest, and how many fail the thinness check
  {
    float amin = 1e9f;
    uint32_t thin = 0;
    for(uint32_t p = 0; p < g.count; p++) {
      uint32_t n = cutgraph_outline(&g, p, 1, 0, 0, xy);
      float cx, cy, a = geom_area(xy, n, &cx, &cy);
      amin = fminf(amin, a);
      int ok = cutgraph_thick_ok(xy, n, v.clear);
      thin += !ok;
      if(!ok) {
        int border = 0;
        for(uint32_t q = g.pstart[p]; q < g.pstart[p + 1]; q++) { const CutEdge * e = g.e + g.pedge[q] / 2; border |= e->left < 0 || e->right < 0; }
        printf("  thin piece %u: area %.2f, %s, centroid %.2f,%.2f\n", p, a, border ? "border" : "inner", cx, cy);
      }
    }
    printf("smallest piece %.2f cells, %u pieces too thin\n", amin, thin);
  }
  // TESS_SPREAD=f: pieces pulled apart (each moved by f x its centroid's offset from the middle)
  float spread = getenv("TESS_SPREAD") ? atof(getenv("TESS_SPREAD")) : 0;
  float * off = calloc(2 * g.count, sizeof(float));
  for(uint32_t p = 0; p < g.count && spread > 0; p++) {
    uint32_t n = cutgraph_outline(&g, p, cell, 0, 0, xy);
    float cx, cy;
    geom_area(xy, n, &cx, &cy);
    off[2 * p] = (cx - cols * cell / 2) * spread;
    off[2 * p + 1] = (cy - rows * cell / 2) * spread;
  }
  // Bounds of everything
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for(uint32_t p = 0; p < g.count; p++) {
    uint32_t n = cutgraph_outline(&g, p, cell, off[2 * p], off[2 * p + 1], xy);
    for(uint32_t i = 0; i < n; i++) {
      x0 = fminf(x0, xy[2 * i]); x1 = fmaxf(x1, xy[2 * i]);
      y0 = fminf(y0, xy[2 * i + 1]); y1 = fmaxf(y1, xy[2 * i + 1]);
    }
  }
  int pad = 10, W = (int)(x1 - x0) + 2 * pad, H = (int)(y1 - y0) + 2 * pad;
  uint8_t * img = malloc((size_t)W * H * 3);
  memset(img, 235, (size_t)W * H * 3);
  static const uint8_t base[3][3] = { { 220, 50, 40 }, { 240, 210, 40 }, { 140, 200, 40 } };
  static const uint8_t quilt[8][3] = { { 232, 72, 60 }, { 238, 160, 70 }, { 226, 214, 70 }, { 160, 196, 70 },
      { 90, 159, 130 }, { 120, 180, 214 }, { 236, 196, 190 }, { 80, 80, 86 } };
  int usequilt = getenv("TESS_QUILT") != NULL;
  uint32_t rs = seed * 2654435761u;
  for(uint32_t p = 0; p < g.count; p++) {
    uint32_t n = cutgraph_outline(&g, p, cell, pad - x0 + off[2 * p], pad - y0 + off[2 * p + 1], xy);
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    for(uint32_t i = 0; i < n; i++) {
      bx0 = fminf(bx0, xy[2 * i]); bx1 = fmaxf(bx1, xy[2 * i]);
      by0 = fminf(by0, xy[2 * i + 1]); by1 = fmaxf(by1, xy[2 * i + 1]);
    }
    rs = rs * 1664525u + 1013904223u;
    float shade = 0.8f + 0.2f * (rs >> 8) / 16777216.0f;
    for(int y = (int)by0 - 1; y <= (int)by1 + 1; y++) {
      for(int x = (int)bx0 - 1; x <= (int)bx1 + 1; x++) {
        if(x < 0 || y < 0 || x >= W || y >= H) continue;
        float px = x + 0.5f, py = y + 0.5f;
        if(!geom_inside(xy, n, px, py)) continue;
        float d = 1e9f;
        for(uint32_t i = 0; i < n; i++) {
          uint32_t j = (i + 1) % n;
          d = fminf(d, geom_pointseg(px, py, xy[2 * i], xy[2 * i + 1], xy[2 * j], xy[2 * j + 1]));
        }
        uint8_t * o = img + ((size_t)y * W + x) * 3;
        float edge = d < 1.2f ? 0.15f : 1;
        const uint8_t * bc = usequilt ? quilt[(p * 2654435761u >> 16) % 8] : base[ori[p]];
        for(int c = 0; c < 3; c++) o[c] = (uint8_t)(bc[c] * shade * edge);
      }
    }
  }
  // TESS_CROP=x0,y0,x1,y1 (cells, puzzle frame; no spread): write only that part
  float cr[4];
  if(getenv("TESS_CROP") && sscanf(getenv("TESS_CROP"), "%f,%f,%f,%f", cr, cr + 1, cr + 2, cr + 3) == 4) {
    int cx0 = (int)(cr[0] * cell - x0 + pad), cy0 = (int)(cr[1] * cell - y0 + pad);
    int cx1 = (int)(cr[2] * cell - x0 + pad), cy1 = (int)(cr[3] * cell - y0 + pad);
    cx0 = cx0 < 0 ? 0 : cx0; cy0 = cy0 < 0 ? 0 : cy0; cx1 = cx1 > W ? W : cx1; cy1 = cy1 > H ? H : cy1;
    stbi_write_png(argv[1], cx1 - cx0, cy1 - cy0, 3, img + ((size_t)cy0 * W + cx0) * 3, W * 3);
  } else {
    stbi_write_png(argv[1], W, H, 3, img, W * 3);
  }
  free(img); free(xy); free(ori); free(off);
  cutgraph_free(&g);
  return 0;
}

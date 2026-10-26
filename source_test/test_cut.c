#include "test.h"
#include "layout.h"
#include "edgegen.h"
#include "imgscale.h"
#include "piecebake.h"
#include "swizzle.h"

#include <stdlib.h>
#include <string.h>

void test_layout(void) {
  uint16_t c, r;
  layout_grid(1024, 768, 500, &c, &r);
  CHECK(abs(c * r - 500) <= 20);
  CHECK(fabsf((float)c / r - 4.0f / 3) < 0.1f);
  layout_grid(768, 1024, 48, &c, &r);
  CHECK(c < r && abs(c * r - 48) <= 4);
  layout_grid(1000, 1000, 108, &c, &r);
  CHECK(c == r || abs(c - r) == 1);

  uint32_t targets[4] = { 48, 108, 252, 500 };
  uint32_t sizes[3][2] = { { 4000, 3000 }, { 1024, 1024 }, { 600, 1400 } };
  for(int s = 0; s < 3; s++) {
    for(int t = 0; t < 4; t++) {
      Layout l;
      CHECK(layout_compute(&l, sizes[s][0], sizes[s][1], targets[t], 1024, 2, EDGEGEN_TABREACH) == 0);
      CHECK(l.pages <= 2);
      CHECK(l.width <= 1024 && l.height <= 1024);
      CHECK(l.width <= l.cropw + 0.01f && l.height <= l.croph + 0.01f);  // never upscale
      CHECK(l.cropx + l.cropw <= sizes[s][0] + 0.01f && l.cropy + l.croph <= sizes[s][1] + 0.01f);
      CHECK(l.tile == l.cell + 2 * l.margin);
      // Last tile fits in its page
      uint16_t pg, x, y;
      layout_tile(&l, l.cols * l.rows - 1, &pg, &x, &y);
      CHECK(pg < l.pages && x + l.tile <= LAYOUT_PAGESIZE && y + l.tile <= LAYOUT_PAGESIZE);
      printf("  %4ux%-4u %3u -> %2ux%-2u cell %3u tile %3u pages %u\n", sizes[s][0], sizes[s][1],
          targets[t], l.cols, l.rows, l.cell, l.tile, l.pages);
    }
  }
  Layout l;
  CHECK(layout_compute(&l, 0, 100, 48, 1024, 2, EDGEGEN_TABREACH) != 0);
  CHECK(layout_compute(&l, 20, 20, 500, 1024, 2, EDGEGEN_TABREACH) != 0);  // too small to cut that many
}

void test_edgegen(void) {
  EdgeGen eg, eg2;
  CHECK(edgegen_init(&eg, 5, 4, 42, 0) == 0);
  CHECK(edgegen_init(&eg2, 5, 4, 42, 0) == 0);
  CHECK(memcmp(eg.h, eg2.h, sizeof(EdgeTab) * 5 * 3) == 0);
  edgegen_free(&eg2);

  float a[EDGEGEN_MAXPOINTS * 2], b[EDGEGEN_MAXPOINTS * 2];
  const float cell = 40;
  // Corner piece: two flat sides, so fewer points
  int nc = edgegen_outline(&eg, 0, 0, cell, 0, 0, a);
  CHECK(nc == 2 + 2 * 3 * EDGEGEN_SEGMENTS);
  int ni = edgegen_outline(&eg, 2, 2, cell, 0, 0, a);
  CHECK(ni == EDGEGEN_MAXPOINTS);

  // Every point stays inside the tab margin, and outlines are clockwise (positive area, y down)
  int inbounds = 1, clockwise = 1;
  float minarea = 1e9f, maxarea = 0;
  for(int r = 0; r < 4; r++) {
    for(int c = 0; c < 5; c++) {
      int n = edgegen_outline(&eg, c, r, cell, 0, 0, a);
      float m = cell * 0.35f, area = 0;
      for(int i = 0; i < n; i++) {
        if(a[i*2] < -m || a[i*2] > cell + m || a[i*2+1] < -m || a[i*2+1] > cell + m) inbounds = 0;
        int j = (i + 1) % n;
        area += a[i*2] * a[j*2+1] - a[j*2] * a[i*2+1];
      }
      area /= 2;
      if(area <= 0) clockwise = 0;
      if(area < minarea) minarea = area;
      if(area > maxarea) maxarea = area;
    }
  }
  CHECK(inbounds);
  CHECK(clockwise);
  // Tabs add and blanks remove roughly the same area
  CHECK(minarea > cell * cell * 0.6f && maxarea < cell * cell * 1.4f);

  // Shared edges match: right side of (1,1) == left side of (2,1) reversed, in world space.
  // Place both in world coordinates and check the right piece's left-side points all
  // appear in the left piece's outline.
  int na = edgegen_outline(&eg, 1, 1, cell, 1 * cell, 1 * cell, a);
  int nb = edgegen_outline(&eg, 2, 1, cell, 2 * cell, 1 * cell, b);
  int matched = 0;
  for(int i = 0; i < nb; i++) {
    if(fabsf(b[i*2] - 2 * cell) > cell * 0.4f) continue; // only points near the shared edge
    if(b[i*2+1] <= cell + 0.01f || b[i*2+1] >= 2 * cell - 0.01f) continue; // skip corners
    for(int j = 0; j < na; j++) {
      if(fabsf(a[j*2] - b[i*2]) < 1e-3f && fabsf(a[j*2+1] - b[i*2+1]) < 1e-3f) { matched++; break; }
    }
  }
  CHECK(matched >= 3 * EDGEGEN_SEGMENTS - 1);
  // Same for horizontal: bottom of (3,0) vs top of (3,1)
  na = edgegen_outline(&eg, 3, 0, cell, 3 * cell, 0, a);
  nb = edgegen_outline(&eg, 3, 1, cell, 3 * cell, cell, b);
  matched = 0;
  for(int i = 0; i < nb; i++) {
    if(fabsf(b[i*2+1] - cell) > cell * 0.4f) continue;
    if(b[i*2] <= 3 * cell + 0.01f || b[i*2] >= 4 * cell - 0.01f) continue;
    for(int j = 0; j < na; j++) {
      if(fabsf(a[j*2] - b[i*2]) < 1e-3f && fabsf(a[j*2+1] - b[i*2+1]) < 1e-3f) { matched++; break; }
    }
  }
  CHECK(matched >= 3 * EDGEGEN_SEGMENTS - 1);
  edgegen_free(&eg);
}

// Irregular cut: same guarantees as classic, plus corners actually move and the border
// stays straight
void test_edgegen_irregular(void) {
  const uint16_t cols = 7, rows = 5;
  const float cell = 40, jitter = EDGEGEN_IRREGULAR;
  EdgeGen eg;
  CHECK(edgegen_init(&eg, cols, rows, 99, jitter) == 0);
  int moved = 0;
  for(int i = 0; i < (cols + 1) * (rows + 1); i++) if(eg.corners[i * 2] != 0) moved++;
  CHECK(moved > (cols - 1) * (rows + 1) / 2);
  float a[EDGEGEN_MAXPOINTS * 2], b[EDGEGEN_MAXPOINTS * 2];
  int inbounds = 1, border = 1, clockwise = 1;
  float reach = (EDGEGEN_TABREACH + jitter) * cell;
  for(int r = 0; r < rows; r++) for(int c = 0; c < cols; c++) {
    int n = edgegen_outline(&eg, c, r, cell, c * cell, r * cell, a);
    float area = 0;
    for(int i = 0; i < n; i++) {
      float x = a[i*2], y = a[i*2+1];
      if(x < c * cell - reach || x > (c + 1) * cell + reach || y < r * cell - reach || y > (r + 1) * cell + reach) inbounds = 0;
      // Nothing may poke outside the puzzle rectangle
      if(x < -1e-3f || y < -1e-3f || x > cols * cell + 1e-3f || y > rows * cell + 1e-3f) border = 0;
      int j = (i + 1) % n;
      area += a[i*2] * a[j*2+1] - a[j*2] * a[i*2+1];
    }
    if(area <= 0) clockwise = 0;
  }
  CHECK(inbounds);
  CHECK(border);
  CHECK(clockwise);
  // Shared edges: every point of (2,2) that lies near (3,2) must appear in (3,2), and the same
  // for (2,2) vs (2,3). Points are compared in world space.
  int pairs[2][2] = { { 3, 2 }, { 2, 3 } };
  for(int k = 0; k < 2; k++) {
    int na = edgegen_outline(&eg, 2, 2, cell, 2 * cell, 2 * cell, a);
    int nb = edgegen_outline(&eg, pairs[k][0], pairs[k][1], cell, pairs[k][0] * cell, pairs[k][1] * cell, b);
    int shared = 0;
    for(int i = 0; i < na; i++) for(int j = 0; j < nb; j++) {
      if(fabsf(a[i*2] - b[j*2]) < 1e-3f && fabsf(a[i*2+1] - b[j*2+1]) < 1e-3f) { shared++; break; }
    }
    // A full tab edge has 3 * SEGMENTS + 1 points including both corners
    CHECK(shared == 3 * EDGEGEN_SEGMENTS + 1);
  }
  edgegen_free(&eg);
}

void test_imgscale(void) {
  // 4x2 RGB -> 2x1 averages 2x2 blocks
  uint8_t src[4 * 2 * 3], dst[2 * 1 * 3];
  for(int i = 0; i < 8; i++) { src[i*3] = i < 4 ? (i % 4 < 2 ? 0 : 200) : (i % 4 < 2 ? 100 : 100); src[i*3+1] = 50; src[i*3+2] = 255; }
  CHECK(imgscale_area(src, 4, 2, 3, 0, 0, 4, 2, dst, 2, 1) == 0);
  CHECK(dst[0] == 50 && dst[3] == 150);
  CHECK(dst[1] == 50 && dst[2] == 255);
  // Fractional: 3 -> 2 pixels, a constant image stays constant
  uint8_t c3[3] = { 77, 77, 77 }, c2[2];
  CHECK(imgscale_area(c3, 3, 1, 1, 0, 0, 3, 1, c2, 2, 1) == 0);
  CHECK(c2[0] == 77 && c2[1] == 77);
  // Crop offset picks the right region
  uint8_t g[4] = { 10, 20, 30, 40 }, o[1];
  CHECK(imgscale_area(g, 4, 1, 1, 2, 0, 2, 1, o, 1, 1) == 0);
  CHECK(o[0] == 35);
  // Fit: shrink to the tighter side, never upscale, never 0
  uint16_t w, h;
  imgscale_fit(1000, 500, 320, 200, &w, &h);
  CHECK(w == 320 && h == 160);
  imgscale_fit(500, 1000, 320, 200, &w, &h);
  CHECK(w == 100 && h == 200);
  imgscale_fit(100, 50, 320, 200, &w, &h);
  CHECK(w == 100 && h == 50);
  imgscale_fit(5000, 1, 320, 200, &w, &h);
  CHECK(w == 320 && h == 1);
}

void test_swizzle(void) {
  CHECK(swizzle_offset(16, 0, 0) == 0);
  CHECK(swizzle_offset(16, 1, 0) == 1);
  CHECK(swizzle_offset(16, 0, 1) == 2);
  CHECK(swizzle_offset(16, 7, 7) == 63);
  CHECK(swizzle_offset(16, 8, 0) == 64);
  CHECK(swizzle_offset(16, 0, 8) == 128);
  // Bijective over a texture
  uint32_t tex[16 * 16];
  uint8_t seen[256] = {0};
  int ok = 1;
  for(int y = 0; y < 16; y++) for(int x = 0; x < 16; x++) {
    uint32_t o = swizzle_offset(16, x, y);
    if(o >= 256 || seen[o]) ok = 0; else seen[o] = 1;
  }
  CHECK(ok);
  uint32_t src[3 * 2] = { 1, 2, 3, 4, 5, 6 };
  memset(tex, 0, sizeof(tex));
  swizzle_copy(tex, 16, 9, 3, src, 3, 2, 3);
  CHECK(tex[swizzle_offset(16, 9, 3)] == 1 && tex[swizzle_offset(16, 11, 4)] == 6);
  uint8_t rgb[2 * 2 * 3] = { 1, 2, 3,  4, 5, 6,  7, 8, 9,  10, 11, 12 };
  swizzle_copy_rgb(tex, 16, 8, 8, rgb, 2, 2);
  CHECK(tex[swizzle_offset(16, 8, 8)] == SWIZZLE_RGBA(1, 2, 3, 255));
  CHECK(tex[swizzle_offset(16, 9, 9)] == SWIZZLE_RGBA(10, 11, 12, 255));
}

void test_piecebake(void) {
  // Square outline covering exactly pixels [2,6) x [2,6) of an 8x8 tile
  PieceBaker pb;
  CHECK(piecebake_init(&pb, 8) == 0);
  float sq[8] = { 2, 2, 6, 2, 6, 6, 2, 6 };
  uint8_t img[8 * 8 * 3];
  for(int i = 0; i < 64; i++) { img[i*3] = 200; img[i*3+1] = 100; img[i*3+2] = i; }
  uint32_t out[64];
  piecebake_tile(&pb, sq, 4, img, 8, 8, 0, 0, out);
  CHECK((out[3 * 8 + 3] & 0xFF) == 255);   // inside
  CHECK((out[0] & 0xFF) == 0);             // outside
  CHECK((out[1 * 8 + 3] & 0xFF) == 0);
  CHECK((out[3 * 8 + 3] >> 24) == 200);    // interior not darkened
  CHECK((out[2 * 8 + 3] >> 24) < 200);     // boundary pixel darkened (outline)
  CHECK(((out[4 * 8 + 4] >> 8) & 0xFF) == 36);  // blue channel = pixel index from image
  // Half-pixel edge gives half coverage
  float half[8] = { 2.5f, 2, 6, 2, 6, 6, 2.5f, 6 };
  piecebake_tile(&pb, half, 4, img, 8, 8, 0, 0, out);
  int a = out[3 * 8 + 2] & 0xFF;
  CHECK(a > 120 && a < 135);
  piecebake_free(&pb);
}

// Not a check: renders a whole small puzzle, pieces pulled slightly apart, to a PPM
// so the shapes can be looked at.
void test_dump_cut(const char * path, int irregular) {
  const int cols = 6, rows = 4;
  float jitter = irregular ? EDGEGEN_IRREGULAR : 0;
  Layout l;
  layout_compute(&l, 600, 400, cols * rows, 1024, 2, EDGEGEN_TABREACH + jitter);
  uint8_t * img = malloc(l.width * l.height * 3);
  for(uint32_t y = 0; y < l.height; y++) for(uint32_t x = 0; x < l.width; x++) {
    uint8_t * p = img + (y * l.width + x) * 3;
    p[0] = x * 255 / l.width; p[1] = y * 255 / l.height; p[2] = ((x / 20 + y / 20) & 1) ? 200 : 90;
  }
  EdgeGen eg;
  edgegen_init(&eg, l.cols, l.rows, 7, jitter);
  PieceBaker pb;
  piecebake_init(&pb, l.tile);
  uint32_t * tile = malloc(sizeof(uint32_t) * l.tile * l.tile);
  int gap = l.cell / 6;
  uint32_t W = l.cols * (l.cell + gap) + 2 * l.margin, H = l.rows * (l.cell + gap) + 2 * l.margin;
  uint8_t * canvas = malloc(W * H * 3);
  memset(canvas, 40, W * H * 3);
  float xy[EDGEGEN_MAXPOINTS * 2];
  for(int r = 0; r < l.rows; r++) for(int c = 0; c < l.cols; c++) {
    int n = edgegen_outline(&eg, c, r, l.cell, l.margin, l.margin, xy);
    piecebake_tile(&pb, xy, n, img, l.width, l.height, c * l.cell - l.margin, r * l.cell - l.margin, tile);
    int bx = c * (l.cell + gap), by = r * (l.cell + gap);
    for(int y = 0; y < l.tile; y++) for(int x = 0; x < l.tile; x++) {
      uint32_t px = tile[y * l.tile + x];
      float al = (px & 0xFF) / 255.0f;
      uint8_t * d = canvas + ((by + y) * W + bx + x) * 3;
      d[0] = d[0] * (1 - al) + (px >> 24) * al;
      d[1] = d[1] * (1 - al) + ((px >> 16) & 0xFF) * al;
      d[2] = d[2] * (1 - al) + ((px >> 8) & 0xFF) * al;
    }
  }
  FILE * f = fopen(path, "wb");
  if(f) {
    fprintf(f, "P6\n%u %u\n255\n", W, H);
    fwrite(canvas, 1, W * H * 3, f);
    fclose(f);
  }
  free(canvas); free(tile); free(img);
  piecebake_free(&pb);
  edgegen_free(&eg);
}

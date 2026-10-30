#include "test.h"
#include "layout.h"
#include "edgegen.h"
#include "cutter.h"
#include "imgscale.h"
#include "piecebake.h"
#include "swizzle.h"
#include "cutsample.h"
#include "woodcut.h"

#include <stdlib.h>
#include <string.h>

#define PINNED_CLASSIC 0x6371022Fu     // Same as 1.0-1.1 edgegen
#define PINNED_IRREGULAR 0x0CB062D3u
#define PINNED_WILD 0x6CE7D16Fu
#define PINNED_RANDOM 0xB45D299Bu      // 1.2
#define PINNED_RANDRECT 0x16C37493u    // 1.3
#define PINNED_WHIMSY { { 0x8C3BD049u, 0x881A00CEu }, { 0xFFEF07BBu, 0xBEC93799u }, \
    { 0x33ED2971u, 0xB5F9C31Fu }, { 0x987CDF7Bu, 0xBEC67F6Cu }, \
    { 0x33BEE477u, 0x0450611Bu } }   // 1.2 (Random Rect 1.3), style x Few/Many

void test_layout(void) {
  uint16_t c, r;
  layout_grid(1024, 768, 500, &c, &r);
  CHECK(abs(c * r - 500) <= 20);
  CHECK(fabsf((float)c / r - 4.0f / 3) < 0.1f);
  layout_grid(768, 1024, 48, &c, &r);
  CHECK(c < r && abs(c * r - 48) <= 4);
  layout_grid(1000, 1000, 108, &c, &r);
  CHECK(c == r || abs(c - r) == 1);

  // Every cut style packs into 3 pages at every count, with no overlaps, never upscaling
  uint32_t targets[4] = { 50, 100, 250, 500 };
  uint32_t sizes[4][2] = { { 4000, 3000 }, { 1024, 1024 }, { 600, 1400 }, { 1024, 768 } };
  const char * names[CUT_STYLES] = { "classic", "irregular", "wild", "random", "randrect" };
  for(int s = 0; s < 4; s++) {
    for(int t = 0; t < 4; t++) {
      printf("  %4ux%-4u %3u:", sizes[s][0], sizes[s][1], targets[t]);
      for(uint8_t st = 0; st < CUT_STYLES; st++) {
        Layout l;
        CHECK(layout_crop(&l, sizes[s][0], sizes[s][1], targets[t]) == 0);
        Cutter cut;
        CHECK(cutter_init(&cut, st, WHIMSY_NONE, 0, l.cols, l.rows, 1000 + t) == 0);
        float * boxes = malloc(sizeof(float) * 4 * cut.count);
        LayoutTile * tiles = malloc(sizeof(LayoutTile) * cut.count);
        LayoutTile * again = malloc(sizeof(LayoutTile) * cut.count);
        CHECK(cutter_bounds(&cut, boxes) == 0);
        CHECK(layout_fit(&l, boxes, cut.count, 1024, 3, tiles) == 0);
        Layout l2 = l;
        CHECK(layout_fit(&l2, boxes, cut.count, 1024, 3, again) == 0);
        int same = l2.cell == l.cell;
        for(uint32_t p = 0; p < cut.count; p++) {
          const LayoutTile * a = tiles + p, * b = again + p;
          same &= a->page == b->page && a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h &&
              a->ox == b->ox && a->oy == b->oy;
        }
        CHECK(same);
        CHECK(l.pages >= 1 && l.pages <= 3);
        CHECK(l.width <= 1024 && l.height <= 1024);
        CHECK(l.width <= l.cropw + 0.01f && l.height <= l.croph + 0.01f);  // never upscale
        CHECK(l.cropx + l.cropw <= sizes[s][0] + 0.01f && l.cropy + l.croph <= sizes[s][1] + 0.01f);
        int inside = 1, covers = 1, overlap = 0, maxok = 1;
        for(uint32_t p = 0; p < cut.count; p++) {
          const LayoutTile * a = tiles + p;
          const float * b = boxes + 4 * p;
          inside &= a->page < l.pages && a->x + a->w <= LAYOUT_PAGESIZE && a->y + a->h <= LAYOUT_PAGESIZE;
          covers &= a->ox <= b[0] * l.cell - LAYOUT_PAD + 0.01f && a->oy <= b[1] * l.cell - LAYOUT_PAD + 0.01f &&
              a->ox + a->w >= b[2] * l.cell + LAYOUT_PAD - 0.01f && a->oy + a->h >= b[3] * l.cell + LAYOUT_PAD - 0.01f;
          maxok &= a->w <= l.maxw && a->h <= l.maxh;
          for(uint32_t q = p + 1; q < cut.count; q++) {
            const LayoutTile * c = tiles + q;
            overlap += a->page == c->page && a->x < c->x + c->w && c->x < a->x + a->w &&
                a->y < c->y + c->h && c->y < a->y + a->h;
          }
        }
        CHECK(inside && covers && overlap == 0 && maxok);
        printf("  %s %3u/%u", names[st], l.cell, l.pages);
        free(boxes); free(tiles); free(again);
        cutter_free(&cut);
      }
      printf("  (cell/pages)\n");
    }
  }
  Layout l;
  CHECK(layout_crop(&l, 0, 100, 48) != 0);
  // Too small to cut that many: fails cleanly (a 20x20 image can't give 500 pieces >= 8 px)
  CHECK(layout_crop(&l, 20, 20, 500) == 0);
  float box[4] = { 0, 0, 1, 1 };
  LayoutTile one;
  CHECK(layout_fit(&l, box, 1, 1024, 3, &one) == 2);
}

// Pinned output: saves only store group positions, so if a generator's output changes,
// existing saves for that style no longer fit their pieces. These hashes catch accidental
// changes; update them only together with a deliberate, versioned change.
static uint32_t cut_hash(uint8_t style, uint8_t whimsy, uint16_t cols, uint16_t rows, uint32_t seed) {
  Cutter cut;
  if(cutter_init(&cut, style, whimsy, WHIMSY_SET_CUTE, cols, rows, seed)) return 0;
  uint32_t h = cutter_hash(&cut);
  cutter_free(&cut);
  return h;
}

void test_cut_pinned(void) {
  const uint32_t want[CUT_STYLES] = { PINNED_CLASSIC, PINNED_IRREGULAR, PINNED_WILD, PINNED_RANDOM, PINNED_RANDRECT };
  for(uint8_t st = 0; st < CUT_STYLES; st++) {
    uint32_t h = cut_hash(st, WHIMSY_NONE, 13, 9, 4242);
    if(h != want[st]) printf("  style %u hash %08lX\n", st, (unsigned long)h);
    CHECK(h == want[st]);
  }
  // With whimsies (style x Few/Many)
  const uint32_t wantw[CUT_STYLES][2] = PINNED_WHIMSY;
  for(uint8_t st = 0; st < CUT_STYLES; st++) {
    for(uint8_t wh = 0; wh < 2; wh++) {
      uint32_t h = cut_hash(st, WHIMSY_FEW + wh, 13, 9, 4242);
      if(h != wantw[st][wh]) printf("  style %u whimsy %u hash %08lX\n", st, WHIMSY_FEW + wh, (unsigned long)h);
      CHECK(h == wantw[st][wh]);
    }
  }
}

void test_edgegen(void) {
  EdgeGen eg, eg2;
  CHECK(edgegen_init(&eg, 5, 4, 42, edgegen_style(EDGEGEN_STYLE_CLASSIC)) == 0);
  CHECK(edgegen_init(&eg2, 5, 4, 42, edgegen_style(EDGEGEN_STYLE_CLASSIC)) == 0);
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

// Irregular / wild cut: same guarantees as classic, plus corners actually move and the
// border stays straight
static void test_edgegen_jittered(uint8_t stylenum) {
  const uint16_t cols = 7, rows = 5;
  const float cell = 40;
  const EdgeStyle * style = edgegen_style(stylenum);
  EdgeGen eg;
  CHECK(edgegen_init(&eg, cols, rows, 99, style) == 0);
  int moved = 0;
  for(int i = 0; i < (cols + 1) * (rows + 1); i++) if(eg.corners[i * 2] != 0) moved++;
  CHECK(moved > (cols - 1) * (rows + 1) / 2);
  float a[EDGEGEN_MAXPOINTS * 2], b[EDGEGEN_MAXPOINTS * 2];
  int inbounds = 1, border = 1, clockwise = 1;
  float reach = style->reach * cell;
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

void test_edgegen_irregular(void) {
  test_edgegen_jittered(EDGEGEN_STYLE_IRREGULAR);
  test_edgegen_jittered(EDGEGEN_STYLE_WILD);
  // Validation: full size wild cuts come out with every piece passing the checks
  int allok = 1;
  for(uint32_t seed = 1; seed <= 4; seed++) {
    EdgeGen eg;
    CHECK(edgegen_init(&eg, 26, 19, seed * 7919, edgegen_style(EDGEGEN_STYLE_WILD)) == 0);
    for(uint16_t r = 0; r < 19; r++) for(uint16_t c = 0; c < 26; c++) if(!edgegen_piece_ok(&eg, c, r)) allok = 0;
    edgegen_free(&eg);
  }
  CHECK(allok);
  // ...and the check does catch a broken piece: drag a corner across the cell
  EdgeGen eg;
  CHECK(edgegen_init(&eg, 4, 4, 5, edgegen_style(EDGEGEN_STYLE_CLASSIC)) == 0);
  CHECK(edgegen_piece_ok(&eg, 1, 1));
  eg.corners[2 * (1 * 5 + 1)] = 0.9f;
  eg.corners[2 * (1 * 5 + 1) + 1] = 0.9f;
  CHECK(!edgegen_piece_ok(&eg, 1, 1));
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
  CHECK(piecebake_init(&pb, 8, 8) == 0);
  float sq[8] = { 2, 2, 6, 2, 6, 6, 2, 6 };
  uint8_t img[8 * 8 * 3];
  for(int i = 0; i < 64; i++) { img[i*3] = 200; img[i*3+1] = 100; img[i*3+2] = i; }
  uint32_t out[64];
  piecebake_tile(&pb, sq, 4, img, 8, 8, 0, 0, 8, 8, out);
  CHECK((out[3 * 8 + 3] & 0xFF) == 255);   // inside
  CHECK((out[0] & 0xFF) == 0);             // outside
  CHECK((out[1 * 8 + 3] & 0xFF) == 0);
  CHECK((out[3 * 8 + 3] >> 24) == 200);    // interior not darkened
  CHECK((out[2 * 8 + 3] >> 24) < 200);     // boundary pixel darkened (outline)
  CHECK(((out[4 * 8 + 4] >> 8) & 0xFF) == 36);  // blue channel = pixel index from image
  // Half-pixel edge gives half coverage
  float half[8] = { 2.5f, 2, 6, 2, 6, 6, 2.5f, 6 };
  piecebake_tile(&pb, half, 4, img, 8, 8, 0, 0, 8, 8, out);
  int a = out[3 * 8 + 2] & 0xFF;
  CHECK(a > 120 && a < 135);
  piecebake_free(&pb);
}

// Not a check: renders a whole small puzzle, pieces pulled slightly apart, to a PPM
// so the shapes can be looked at.
void test_dump_cut(const char * path, uint8_t stylenum) {
  Layout l;
  layout_crop(&l, 600, 400, 24);
  Cutter cut;
  woodcut_init(&cut, stylenum, WHIMSY_NONE, 0, l.cols, l.rows, 7);
  float * boxes = malloc(sizeof(float) * 4 * cut.count);
  LayoutTile * tiles = malloc(sizeof(LayoutTile) * cut.count);
  cutter_bounds(&cut, boxes);
  layout_fit(&l, boxes, cut.count, 1024, 3, tiles);
  uint8_t * img = malloc(l.width * l.height * 3);
  for(uint32_t y = 0; y < l.height; y++) for(uint32_t x = 0; x < l.width; x++) {
    uint8_t * p = img + (y * l.width + x) * 3;
    p[0] = x * 255 / l.width; p[1] = y * 255 / l.height; p[2] = ((x / 20 + y / 20) & 1) ? 200 : 90;
  }
  PieceBaker pb;
  piecebake_init(&pb, l.maxw, l.maxh);
  uint32_t * tile = malloc(sizeof(uint32_t) * l.maxw * l.maxh);
  float * xy = malloc(sizeof(float) * 2 * cut.maxpoints);
  // Pieces pulled apart: each tile center moves out from the puzzle's top-left by 1/6
  const float spread = 1 / 6.0f;
  uint32_t W = l.width * (1 + spread) + l.maxw, H = l.height * (1 + spread) + l.maxh;
  uint8_t * canvas = malloc(W * H * 3);
  memset(canvas, 40, W * H * 3);
  for(uint32_t i = 0; i < cut.count; i++) {
    const LayoutTile * t = tiles + i;
    uint32_t n = cutter_outline(&cut, i, l.cell, -t->ox, -t->oy, xy);
    piecebake_tile(&pb, xy, n, img, l.width, l.height, t->ox, t->oy, t->w, t->h, tile);
    float cx = t->ox + t->w / 2.0f, cy = t->oy + t->h / 2.0f;
    int bx = (int)(cx * (1 + spread) - t->w / 2.0f + l.maxw / 2), by = (int)(cy * (1 + spread) - t->h / 2.0f + l.maxh / 2);
    for(int y = 0; y < t->h; y++) for(int x = 0; x < t->w; x++) {
      if(bx + x < 0 || by + y < 0 || bx + x >= (int)W || by + y >= (int)H) continue;
      uint32_t px = tile[y * t->w + x];
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
  free(canvas); free(tile); free(img); free(xy); free(boxes); free(tiles);
  piecebake_free(&pb);
  cutter_free(&cut);
}

// Menu cut sample: lines on a transparent image, same settings = same picture
void test_cutsample(void) {
  uint16_t w = 320, h = 200;
  uint32_t * a = malloc(sizeof(uint32_t) * w * h), * b = malloc(sizeof(uint32_t) * w * h);
  for(uint8_t st = 0; st < CUT_STYLES; st++) {
    CHECK(cutsample_draw(st, WHIMSY_MANY, WHIMSY_SET_CUTE, w, h, a) == 0);
    CHECK(cutsample_draw(st, WHIMSY_MANY, WHIMSY_SET_CUTE, w, h, b) == 0);
    CHECK(memcmp(a, b, sizeof(uint32_t) * w * h) == 0);
    uint32_t lines = 0, clear = 0;
    for(size_t i = 0; i < (size_t)w * h; i++) {
      lines += (a[i] & 0xFF) >= CUTSAMPLE_LINEALPHA / 2;
      clear += a[i] == 0;
    }
    // Lines everywhere but mostly see-through (48 pieces: 18% line, 66% clear)
    CHECK(lines > (uint32_t)(w * h) / 20 && clear > (uint32_t)(w * h) / 2);
  }
  CHECK(cutsample_draw(CUT_STYLE_CLASSIC, WHIMSY_NONE, 0, 200, 320, a) == 0);  // Portrait
  CHECK(cutsample_draw(CUT_STYLE_CLASSIC, WHIMSY_NONE, 0, 0, 10, a) != 0);
  free(a);
  free(b);
}

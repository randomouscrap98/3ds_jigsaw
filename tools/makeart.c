// App art generator: bevelled gold puzzle pieces on textured green felt.
//   make -f Makefile.host build_host/makeart
//   ./build_host/makeart icon icon.png [preview.png]          48x48 HOME menu icon
//   ./build_host/makeart banner meta/banner.png [preview.png] 256x128 CIA banner
// Pieces (and their shadows) are rendered at a supersampled size and averaged down; the
// felt and the wood frame are made at the final size so their texture survives. Previews
// are the output scaled up (nearest neighbour).
#include "edgegen.h"
#include "imgscale.h"
#include "rng.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// ---- Icon ----
#define ICON_SIZE 48
#define ICON_SS 8                       // Supersampling for the piece layer
#define ICON_CELL 0.54f                 // Piece cell size, fraction of the icon
#define ICON_TAB 0.125f                 // Tab size (chunkier than the game's 0.1 so it reads at 48px)
#define ICON_NUDGE_X 0.02f              // Manual nudge after centering, fraction of the icon (+ = right)
#define ICON_NUDGE_Y 0.02f              // (+ = down)
#define ICON_BEVEL 0.11f                // Bevel width, fraction of the cell
#define ICON_SHADOW_OFF 0.035f          // Drop shadow offset (down-right), fraction of the icon
#define ICON_SHADOW_BLUR 0.03f          // Drop shadow softness, fraction of the icon
#define ICON_SHADOW_ALPHA 0.55f
#define ICON_PREVIEW_SCALE 6

// ---- Banner (sizes in final pixels) ----
#define BANNER_W 256
#define BANNER_H 128
#define BANNER_SS 4
#define BANNER_FRAME 9                  // Wood frame width
#define BANNER_COLS 7                   // The puzzle being worked on
#define BANNER_ROWS 4
#define BANNER_CELL 17.0f
#define BANNER_X 26.0f                  // Top left of the puzzle on the banner
#define BANNER_Y 27.0f
#define BANNER_SEED 3                   // Cut of the banner puzzle (classic style)
#define BANNER_BEVEL 0.12f              // Fraction of the cell
#define BANNER_FLAT_OFF 0.6f            // Shadow of pieces lying in the puzzle
#define BANNER_FLAT_BLUR 0.7f
#define BANNER_FLAT_ALPHA 0.45f
#define BANNER_LOOSE_OFF 1.6f           // Shadow of loose pieces
#define BANNER_LOOSE_BLUR 1.3f
#define BANNER_LOOSE_ALPHA 0.5f
#define BANNER_PREVIEW_SCALE 3
// Brass nameplate on the bottom of the frame
#define PLATE_TEXT "3DS Jigsaw"
#define PLATE_FONT "tools/fonts/Cinzel-Bold.ttf"   // OFL, see tools/fonts/OFL.txt
#define PLATE_W 84.0f
#define PLATE_H 15.0f
#define PLATE_BOTTOM 126.0f             // Bottom edge of the plate (banner pixels)
#define PLATE_RADIUS 2.5f               // Corner rounding
#define PLATE_BEVEL 1.6f
#define PLATE_TEXT_H 7.5f               // Cap height of the lettering
#define PLATE_SCREW_IN 5.0f             // Screw heads, this far in from each end
#define PLATE_SCREW_R 1.1f
#define PLATE_SHADOW_OFF 0.8f
#define PLATE_SHADOW_BLUR 0.8f
#define PLATE_SHADOW_ALPHA 0.55f

// Pieces not yet placed: grid cell, where it lies (center, banner pixels), angle (degrees).
// Any missing cell not listed here is just not on the table.
typedef struct { int col, row; float x, y, deg; } LoosePiece;
static const LoosePiece banner_loose[] = {
  { 4, 1, 172, 34, 28 },
  { 5, 2, 205, 52, -63 },
  { 6, 1, 183, 82, 141 },
  { 3, 2, 222, 96, 12 },
  // { 5, 1, 156, 97, -22 },        // Removed: crowded the nameplate
  { 4, 2, 229, 26, 77 },
};
static const int banner_missing[][2] = { { 4, 1 }, { 5, 1 }, { 6, 1 }, { 4, 2 }, { 5, 2 }, { 3, 2 }, { 6, 2 } };

static const float felt[3] = { 46, 64, 58 };   // Same as the game's green felt (render.c)
static const float gold[3] = { 212, 168, 58 };
static const float wood[3] = { 120, 80, 46 };
static const float engrave[3] = { 74, 50, 18 };

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static int inside(const float * xy, int n, float x, float y) {
  int in = 0;
  for(int i = 0, j = n - 1; i < n; j = i++) {
    float xi = xy[i * 2], yi = xy[i * 2 + 1], xj = xy[j * 2], yj = xy[j * 2 + 1];
    if((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
  }
  return in;
}

static float edgedist(const float * xy, int n, float x, float y) {
  float best = 1e30f;
  for(int i = 0, j = n - 1; i < n; j = i++) {
    float ax = xy[j * 2], ay = xy[j * 2 + 1], vx = xy[i * 2] - ax, vy = xy[i * 2 + 1] - ay;
    float l = vx * vx + vy * vy, t = l > 0 ? clampf(((x - ax) * vx + (y - ay) * vy) / l, 0, 1) : 0;
    float dx = x - ax - t * vx, dy = y - ay - t * vy, d = dx * dx + dy * dy;
    if(d < best) best = d;
  }
  return sqrtf(best);
}

// Smooth value noise in [0, 1)
static float hashf(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return ((h ^ (h >> 16)) & 0xFFFFFF) / (float)0x1000000;
}
static float vnoise(float x, float y, uint32_t seed) {
  int ix = (int)floorf(x), iy = (int)floorf(y);
  float fx = x - ix, fy = y - iy;
  fx = fx * fx * (3 - 2 * fx);
  fy = fy * fy * (3 - 2 * fy);
  float a = hashf(ix, iy, seed), b = hashf(ix + 1, iy, seed), c = hashf(ix, iy + 1, seed), d = hashf(ix + 1, iy + 1, seed);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

// ---- Piece layer: supersampled, premultiplied RGBA. Shadows first, then pieces. ----
typedef struct {
  int w, h;
  float * rgb;      // 3 per pixel, 0..255 (premultiplied; pieces are opaque)
  float * alpha;
  float * shadow;   // Shadow alpha, max over everything cast so far
} Layer;

static void layer_init(Layer * l, int w, int h) {
  l->w = w;
  l->h = h;
  l->rgb = calloc((size_t)w * h * 3, sizeof(float));
  l->alpha = calloc((size_t)w * h, sizeof(float));
  l->shadow = calloc((size_t)w * h, sizeof(float));
}

static void layer_free(Layer * l) {
  free(l->rgb);
  free(l->alpha);
  free(l->shadow);
}

// Shadow of a set of outlines: their coverage moved down-right by off, box blurred 3 times
// with radius rad (~gaussian), scaled by strength
static void layer_shadow(Layer * l, float ** outlines, const int * ns, int count, int off, int rad, float strength) {
  int W = l->w, H = l->h;
  float * cov = calloc((size_t)W * H, sizeof(float)), * tmp = calloc((size_t)W * H, sizeof(float));
  for(int k = 0; k < count; k++) {
    const float * xy = outlines[k];
    int n = ns[k];
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for(int i = 0; i < n; i++) {
      minx = fminf(minx, xy[i * 2]); maxx = fmaxf(maxx, xy[i * 2]);
      miny = fminf(miny, xy[i * 2 + 1]); maxy = fmaxf(maxy, xy[i * 2 + 1]);
    }
    for(int y = (int)fmaxf(0, miny + off); y < H && y <= maxy + off + 1; y++) {
      for(int x = (int)fmaxf(0, minx + off); x < W && x <= maxx + off + 1; x++) {
        if(inside(xy, n, x - off + 0.5f, y - off + 0.5f)) cov[y * W + x] = 1;
      }
    }
  }
  for(int pass = 0; pass < 3; pass++) {
    for(int y = 0; y < H; y++) for(int x = 0; x < W; x++) {
      float s = 0;
      for(int k = -rad; k <= rad; k++) s += cov[y * W + (int)clampf(x + k, 0, W - 1)];
      tmp[y * W + x] = s / (2 * rad + 1);
    }
    for(int y = 0; y < H; y++) for(int x = 0; x < W; x++) {
      float s = 0;
      for(int k = -rad; k <= rad; k++) s += tmp[(int)clampf(y + k, 0, H - 1) * W + x];
      cov[y * W + x] = s / (2 * rad + 1);
    }
  }
  for(int i = 0; i < W * H; i++) l->shadow[i] = fmaxf(l->shadow[i], cov[i] * strength);
  free(cov);
  free(tmp);
}

// Draw one gold piece (outline in layer pixels). bevel and rim are widths in layer pixels.
// The sheen runs across the box (sx, sy, sw, sh), brighter toward its top left.
static void layer_piece(Layer * l, const float * xy, int n, float bevel, float rim,
    float sx, float sy, float sw, float sh) {
  int W = l->w, H = l->h;
  float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
  for(int i = 0; i < n; i++) {
    minx = fminf(minx, xy[i * 2]); maxx = fmaxf(maxx, xy[i * 2]);
    miny = fminf(miny, xy[i * 2 + 1]); maxy = fmaxf(maxy, xy[i * 2 + 1]);
  }
  // Height field over the bounding box (+1 pixel for the gradient): 0 at the edge rising
  // to 1 one bevel width in, smoothstepped
  int x0 = (int)fmaxf(0, minx - 1), y0 = (int)fmaxf(0, miny - 1);
  int x1 = (int)fminf(W - 1, maxx + 1), y1 = (int)fminf(H - 1, maxy + 1);
  int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
  float * height = calloc((size_t)bw * bh, sizeof(float));
  uint8_t * cover = calloc((size_t)bw * bh, 1);
  for(int y = y0; y <= y1; y++) {
    for(int x = x0; x <= x1; x++) {
      if(!inside(xy, n, x + 0.5f, y + 0.5f)) continue;
      float t = clampf(edgedist(xy, n, x + 0.5f, y + 0.5f) / bevel, 0, 1);
      height[(y - y0) * bw + x - x0] = t * t * (3 - 2 * t);
      cover[(y - y0) * bw + x - x0] = 1;
    }
  }
  const float lx = -0.55f, ly = -0.65f, lz = 0.52f;   // Light from the top left
  float ll = sqrtf(lx * lx + ly * ly + lz * lz);
  float hx = lx / ll, hy = ly / ll, hz = lz / ll + 1;  // Half vector (viewer straight on)
  float hl = sqrtf(hx * hx + hy * hy + hz * hz);
  for(int y = y0; y <= y1; y++) {
    for(int x = x0; x <= x1; x++) {
      int bx = x - x0, by = y - y0;
      if(!cover[by * bw + bx]) continue;
      float h = height[by * bw + bx];
      float gx = (height[by * bw + (bx + 1 < bw ? bx + 1 : bx)] - height[by * bw + (bx > 0 ? bx - 1 : bx)]) / 2;
      float gy = (height[(by + 1 < bh ? by + 1 : by) * bw + bx] - height[(by > 0 ? by - 1 : by) * bw + bx]) / 2;
      // Steeper bevel = bigger normal tilt
      float k = bevel * 0.9f;
      float nx = -gx * k, ny = -gy * k, nz = 1, nl = sqrtf(nx * nx + ny * ny + nz * nz);
      nx /= nl; ny /= nl; nz /= nl;
      float diff = clampf((nx * lx + ny * ly + nz * lz) / ll, 0, 1);
      float spec = powf(clampf((nx * hx + ny * hy + nz * hz) / hl, 0, 1), 28);
      // Faint brushed sheen across the flat top, brighter toward the top left
      float sheen = 0.08f * (1 - ((x - sx) / sw + (y - sy) / sh) / 2);
      float shade = 0.35f + 0.85f * diff + sheen;
      float rgb[3];
      for(int i = 0; i < 3; i++) rgb[i] = clampf(gold[i] * shade + 255 * spec * (0.35f + 0.4f * (1 - h)), 0, 255);
      // Thin dark rim right at the edge for contrast against the felt
      float r = clampf(edgedist(xy, n, x + 0.5f, y + 0.5f) / rim, 0, 1);
      for(int i = 0; i < 3; i++) l->rgb[((size_t)y * W + x) * 3 + i] = rgb[i] * (0.45f + 0.55f * r);
      l->alpha[(size_t)y * W + x] = 1;
    }
  }
  free(height);
  free(cover);
}

// Average the layer down to w x h premultiplied RGBA (uint8)
static uint8_t * layer_resolve(Layer * l, int w, int h) {
  int W = l->w, H = l->h;
  uint8_t * big = malloc((size_t)W * H * 4), * small = malloc((size_t)w * h * 4);
  for(int i = 0; i < W * H; i++) {
    uint8_t * p = big + (size_t)i * 4;
    if(l->alpha[i] > 0) {
      for(int c = 0; c < 3; c++) p[c] = (uint8_t)l->rgb[(size_t)i * 3 + c];
      p[3] = 255;
    } else {
      p[0] = p[1] = p[2] = 0;
      p[3] = (uint8_t)(l->shadow[i] * 255 + 0.5f);
    }
  }
  if(imgscale_area(big, W, H, 4, 0, 0, W, H, small, w, h)) exit(1);
  free(big);
  return small;
}

// Felt over the box (x0, y0, w, h) of an RGB image (stride iw): base color, soft vignette,
// blotchy low-frequency + fine grain
static void draw_felt(uint8_t * out, int iw, int x0, int y0, int w, int h) {
  Rng r;
  rng_init(&r, 12345);
  for(int y = y0; y < y0 + h; y++) {
    for(int x = x0; x < x0 + w; x++) {
      float u = (x - x0 + 0.5f) / w - 0.5f, v = (y - y0 + 0.5f) / h - 0.5f;
      float vig = 1.12f - 0.55f * (u * u + v * v);
      float blotch = (vnoise(x / 6.0f, y / 6.0f, 7) - 0.5f) * 0.10f;
      float grain = (rng_float(&r) - 0.5f) * 0.12f;
      float m = vig + blotch + grain;
      for(int i = 0; i < 3; i++) out[(y * iw + x) * 3 + i] = (uint8_t)clampf(felt[i] * m, 0, 255);
    }
  }
}

// Composite the resolved piece layer (premultiplied) over the image
static void composite(uint8_t * out, const uint8_t * layer, int w, int h) {
  for(int i = 0; i < w * h; i++) {
    float a = layer[i * 4 + 3] / 255.0f;
    for(int c = 0; c < 3; c++) out[i * 3 + c] = (uint8_t)clampf(layer[i * 4 + c] + out[i * 3 + c] * (1 - a) + 0.5f, 0, 255);
  }
}

static int write_png(const char * path, const uint8_t * rgb, int w, int h, const char * preview, int scale) {
  stbi_write_png_compression_level = 9;
  if(!stbi_write_png(path, w, h, 3, rgb, w * 3)) return 1;
  if(!preview) return 0;
  int pw = w * scale, ph = h * scale;
  uint8_t * big = malloc((size_t)pw * ph * 3);
  for(int y = 0; y < ph; y++) for(int x = 0; x < pw; x++) {
    memcpy(big + ((size_t)y * pw + x) * 3, rgb + ((y / scale) * w + x / scale) * 3, 3);
  }
  int ok = stbi_write_png(preview, pw, ph, 3, big, pw * 3);
  free(big);
  return ok ? 0 : 1;
}

static int make_icon(const char * path, const char * preview) {
  const int B = ICON_SIZE * ICON_SS;
  // A clean classic piece: 3x3 grid, middle piece, tabs out on top and right, in on the
  // bottom and left, no jitter
  EdgeGen eg;
  if(edgegen_init(&eg, 3, 3, 1, edgegen_style(EDGEGEN_STYLE_CLASSIC))) return 1;
  EdgeTab clean = { .size = ICON_TAB, .a = 0, .b = 0, .c = 0, .d = 0, .e = 0, .flip = 1 };
  for(int i = 0; i < 2 * 3; i++) eg.h[i] = eg.v[i] = clean;
  eg.h[0 * 3 + 1].flip = -1;   // Top edge of (1,1): tab sticks up, out of the piece
  eg.h[1 * 3 + 1].flip = -1;   // Bottom edge: tab goes up, into the piece
  eg.v[1 * 2 + 0].flip = 1;    // Left edge: tab goes right, into the piece
  eg.v[1 * 2 + 1].flip = 1;    // Right edge: tab sticks right, out of the piece
  const float cell = ICON_CELL * B;
  float xy[EDGEGEN_MAXPOINTS * 2];
  int n = edgegen_outline(&eg, 1, 1, cell, 0, 0, xy);
  edgegen_free(&eg);
  // Center on the area centroid (the bounding box is pulled toward the outward tabs, which
  // makes the body look off center)
  float area = 0, cx = 0, cy = 0;
  for(int i = 0, j = n - 1; i < n; j = i++) {
    float cr = xy[j * 2] * xy[i * 2 + 1] - xy[i * 2] * xy[j * 2 + 1];
    area += cr;
    cx += (xy[j * 2] + xy[i * 2]) * cr;
    cy += (xy[j * 2 + 1] + xy[i * 2 + 1]) * cr;
  }
  cx /= 3 * area;
  cy /= 3 * area;
  float sx = B * (0.5f + ICON_NUDGE_X) - cx, sy = B * (0.5f + ICON_NUDGE_Y) - cy;
  for(int i = 0; i < n; i++) {
    xy[i * 2] += sx;
    xy[i * 2 + 1] += sy;
  }
  Layer l;
  layer_init(&l, B, B);
  float * outlines[1] = { xy };
  layer_shadow(&l, outlines, &n, 1, (int)(ICON_SHADOW_OFF * B), (int)(ICON_SHADOW_BLUR * B), ICON_SHADOW_ALPHA);
  layer_piece(&l, xy, n, ICON_BEVEL * cell, ICON_SS * 0.6f, 0, 0, B, B);
  uint8_t * small = layer_resolve(&l, ICON_SIZE, ICON_SIZE);
  layer_free(&l);
  uint8_t * out = malloc(ICON_SIZE * ICON_SIZE * 3);
  draw_felt(out, ICON_SIZE, 0, 0, ICON_SIZE, ICON_SIZE);
  composite(out, small, ICON_SIZE, ICON_SIZE);
  int r = write_png(path, out, ICON_SIZE, ICON_SIZE, preview, ICON_PREVIEW_SCALE);
  free(small);
  free(out);
  return r;
}

// Simple wood picture frame: mitered corners, grain running along each side, light from the
// top left (top/left sides lighter), a highlight on the outer edge, a dark inner lip, and a
// shadow cast onto the felt inside
static void draw_frame(uint8_t * out, int w, int h, int f) {
  for(int y = 0; y < h; y++) {
    for(int x = 0; x < w; x++) {
      int dl = x, dr = w - 1 - x, dt = y, db = h - 1 - y;
      int d = dl < dr ? dl : dr;
      if(dt < d) d = dt;
      if(db < d) d = db;
      uint8_t * p = out + (y * w + x) * 3;
      if(d >= f) {
        // Felt: shadow from the frame along the top and left inner edges
        int it = y - f, il = x - f;
        float s = fminf(it, il);
        if(s < 4) {
          float k = 1 - 0.35f * (1 - s / 4.0f);
          for(int i = 0; i < 3; i++) p[i] = (uint8_t)(p[i] * k);
        }
        continue;
      }
      // Which side (the nearest edge; ties fall on the diagonal = miter)
      int side = d == dt ? 0 : d == db ? 1 : d == dl ? 2 : 3;   // top, bottom, left, right
      float along = side < 2 ? x : y, across = d;
      uint32_t seed = 100 + side;
      float grain = sinf((across + vnoise(along / 22.0f, across / 3.0f, seed) * 5) * 1.6f) * 0.08f;
      float fine = (vnoise(along / 2.5f, across * 1.7f, seed + 10) - 0.5f) * 0.12f;
      float streak = (vnoise(along / 40.0f, across / 1.5f, seed + 20) - 0.5f) * 0.18f;
      float light = side == 0 ? 1.12f : side == 2 ? 1.04f : side == 3 ? 0.86f : 0.78f;
      // Rounded profile: brighter in the middle of the moulding, darker at both edges
      float t = (d + 0.5f) / f;
      float profile = 0.82f + 0.3f * sinf(t * 3.14159265f);
      float m = light * profile * (1 + grain + fine + streak);
      if(d == 0) m *= side == 0 || side == 2 ? 1.25f : 0.8f;  // Outer edge catches the light
      if(d == f - 1) m *= 0.6f;                                // Inner lip
      for(int i = 0; i < 3; i++) p[i] = (uint8_t)clampf(wood[i] * m, 0, 255);
    }
  }
}

// Rounded rectangle outline, clockwise (y down), npts points per corner
static int rounded_rect(float x0, float y0, float x1, float y1, float r, int npts, float * xy) {
  const float cx[4] = { x1 - r, x1 - r, x0 + r, x0 + r }, cy[4] = { y0 + r, y1 - r, y1 - r, y0 + r };
  int n = 0;
  for(int c = 0; c < 4; c++) {
    for(int i = 0; i < npts; i++) {
      float a = (-90 + c * 90 + 90.0f * i / (npts - 1)) * 3.14159265f / 180;
      xy[n * 2] = cx[c] + r * cosf(a);
      xy[n * 2 + 1] = cy[c] + r * sinf(a);
      n++;
    }
  }
  return n;
}

// Text coverage (0..1) into a w x h buffer, cap height capk pixels, centered on (cx, cy)
static int text_coverage(float * cov, int w, int h, const char * text, float capk, float cx, float cy) {
  FILE * f = fopen(PLATE_FONT, "rb");
  if(!f) {
    fprintf(stderr, "can't open %s\n", PLATE_FONT);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char * ttf = malloc(size);
  if(fread(ttf, 1, size, f) != (size_t)size) return 1;
  fclose(f);
  stbtt_fontinfo font;
  if(!stbtt_InitFont(&font, ttf, 0)) return 1;
  int hx0, hy0, hx1, hy1;
  stbtt_GetCodepointBox(&font, 'H', &hx0, &hy0, &hx1, &hy1);
  float scale = capk / hy1;
  // Width with kerning, to center
  float width = 0;
  for(const char * c = text; *c; c++) {
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&font, *c, &adv, &lsb);
    width += adv * scale;
    if(c[1]) width += stbtt_GetCodepointKernAdvance(&font, c[0], c[1]) * scale;
  }
  float pen = cx - width / 2, base = cy + capk / 2;
  for(const char * c = text; *c; c++) {
    int adv, lsb, gw, gh, gx, gy;
    stbtt_GetCodepointHMetrics(&font, *c, &adv, &lsb);
    float fx = pen - floorf(pen), fy = base - floorf(base);
    unsigned char * g = stbtt_GetCodepointBitmapSubpixel(&font, scale, scale, fx, fy, *c, &gw, &gh, &gx, &gy);
    for(int y = 0; y < gh; y++) for(int x = 0; x < gw; x++) {
      int X = (int)floorf(pen) + gx + x, Y = (int)floorf(base) + gy + y;
      if(X >= 0 && Y >= 0 && X < w && Y < h) cov[Y * w + X] = fmaxf(cov[Y * w + X], g[y * gw + x] / 255.0f);
    }
    stbtt_FreeBitmap(g, NULL);
    pen += adv * scale;
    if(c[1]) pen += stbtt_GetCodepointKernAdvance(&font, c[0], c[1]) * scale;
  }
  free(ttf);
  return 0;
}

// Brass nameplate with engraved lettering and two screw heads, drawn over the frame
static int draw_plate(uint8_t * out) {
  const int S = BANNER_SS, W = BANNER_W * S, H = BANNER_H * S;
  float x0 = (BANNER_W - PLATE_W) / 2 * S, x1 = (BANNER_W + PLATE_W) / 2 * S;
  float y1 = PLATE_BOTTOM * S, y0 = y1 - PLATE_H * S;
  float xy[4 * 12 * 2];
  int n = rounded_rect(x0, y0, x1, y1, PLATE_RADIUS * S, 12, xy);
  Layer l;
  layer_init(&l, W, H);
  float * outlines[1] = { xy };
  layer_shadow(&l, outlines, &n, 1, (int)(PLATE_SHADOW_OFF * S), (int)(PLATE_SHADOW_BLUR * S), PLATE_SHADOW_ALPHA);
  layer_piece(&l, xy, n, PLATE_BEVEL * S, S * 0.6f, x0, y0, x1 - x0, y1 - y0);
  // Engraving: letters darkened; their lower right walls catch the light (a light lip
  // where the letter shifted up-left isn't), the upper left ones are in shadow
  float * cov = calloc((size_t)W * H, sizeof(float));
  float cy = (y0 + y1) / 2;
  if(text_coverage(cov, W, H, PLATE_TEXT, PLATE_TEXT_H * S, (x0 + x1) / 2, cy)) return 1;
  const int sh = S * 3 / 4;
  for(int y = (int)y0; y < (int)y1; y++) {
    for(int x = (int)x0; x < (int)x1; x++) {
      size_t i = (size_t)y * W + x;
      if(l.alpha[i] <= 0) continue;
      float c = cov[i];
      float lip = fmaxf(0, cov[(size_t)(y - sh) * W + x - sh] - c);   // just below/right of a stroke
      for(int k = 0; k < 3; k++) {
        float v = l.rgb[i * 3 + k] * (1 - c) + engrave[k] * c;
        l.rgb[i * 3 + k] = clampf(v + 70 * lip, 0, 255);
      }
    }
  }
  // Screw heads: small domes with a slot
  for(int s = 0; s < 2; s++) {
    float sx = s ? x1 - PLATE_SCREW_IN * S : x0 + PLATE_SCREW_IN * S, r = PLATE_SCREW_R * S;
    for(int y = (int)(cy - r - 1); y <= cy + r + 1; y++) for(int x = (int)(sx - r - 1); x <= sx + r + 1; x++) {
      float dx = x + 0.5f - sx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
      if(d > r) continue;
      size_t i = (size_t)y * W + x;
      float lit = 0.75f - 0.35f * (dx + dy) / r;           // Brighter toward the top left
      int slot = fabsf(dx - dy) < S * 0.35f;              // Diagonal slot
      for(int k = 0; k < 3; k++) l.rgb[i * 3 + k] = clampf(slot ? engrave[k] : gold[k] * lit, 0, 255);
    }
  }
  free(cov);
  uint8_t * small = layer_resolve(&l, BANNER_W, BANNER_H);
  layer_free(&l);
  composite(out, small, BANNER_W, BANNER_H);
  free(small);
  return 0;
}

static int make_banner(const char * path, const char * preview) {
  const int S = BANNER_SS, W = BANNER_W * S, H = BANNER_H * S;
  const float cell = BANNER_CELL * S;
  EdgeGen eg;
  if(edgegen_init(&eg, BANNER_COLS, BANNER_ROWS, BANNER_SEED, edgegen_style(EDGEGEN_STYLE_CLASSIC))) return 1;
  const int count = BANNER_COLS * BANNER_ROWS;
  float (*xy)[EDGEGEN_MAXPOINTS * 2] = malloc(sizeof(*xy) * count);
  int * ns = malloc(sizeof(int) * count);
  uint8_t * placed = malloc(count);
  memset(placed, 1, count);
  for(size_t i = 0; i < sizeof(banner_missing) / sizeof(banner_missing[0]); i++) {
    placed[banner_missing[i][1] * BANNER_COLS + banner_missing[i][0]] = 0;
  }
  // Placed pieces at home; loose ones rotated about their cell center and moved
  for(int i = 0; i < count; i++) {
    int col = i % BANNER_COLS, row = i / BANNER_COLS;
    ns[i] = edgegen_outline(&eg, col, row, cell, BANNER_X * S + col * cell, BANNER_Y * S + row * cell, xy[i]);
  }
  edgegen_free(&eg);
  Layer l;
  layer_init(&l, W, H);
  float * flat[BANNER_COLS * BANNER_ROWS], * loose[BANNER_COLS * BANNER_ROWS];
  int nflat[BANNER_COLS * BANNER_ROWS], nloose[BANNER_COLS * BANNER_ROWS], fc = 0, lc = 0;
  for(int i = 0; i < count; i++) {
    if(!placed[i]) continue;
    flat[fc] = xy[i];
    nflat[fc++] = ns[i];
  }
  const int nl = sizeof(banner_loose) / sizeof(banner_loose[0]);
  for(int k = 0; k < nl; k++) {
    const LoosePiece * lp = banner_loose + k;
    int i = lp->row * BANNER_COLS + lp->col;
    float hx = BANNER_X * S + (lp->col + 0.5f) * cell, hy = BANNER_Y * S + (lp->row + 0.5f) * cell;
    float a = lp->deg * 3.14159265f / 180, c = cosf(a), s = sinf(a);
    for(int p = 0; p < ns[i]; p++) {
      float dx = xy[i][p * 2] - hx, dy = xy[i][p * 2 + 1] - hy;
      xy[i][p * 2] = lp->x * S + c * dx - s * dy;
      xy[i][p * 2 + 1] = lp->y * S + s * dx + c * dy;
    }
    loose[lc] = xy[i];
    nloose[lc++] = ns[i];
  }
  layer_shadow(&l, flat, nflat, fc, (int)(BANNER_FLAT_OFF * S), (int)(BANNER_FLAT_BLUR * S), BANNER_FLAT_ALPHA);
  layer_shadow(&l, loose, nloose, lc, (int)(BANNER_LOOSE_OFF * S), (int)(BANNER_LOOSE_BLUR * S), BANNER_LOOSE_ALPHA);
  const float bevel = BANNER_BEVEL * cell, rim = S * 0.6f;
  const float px = BANNER_X * S, py = BANNER_Y * S, pw = BANNER_COLS * cell, ph = BANNER_ROWS * cell;
  // Placed pieces share one sheen across the whole puzzle; loose ones get their own
  for(int k = 0; k < fc; k++) layer_piece(&l, flat[k], nflat[k], bevel, rim, px, py, pw, ph);
  for(int k = 0; k < lc; k++) {
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for(int p = 0; p < nloose[k]; p++) {
      minx = fminf(minx, loose[k][p * 2]); maxx = fmaxf(maxx, loose[k][p * 2]);
      miny = fminf(miny, loose[k][p * 2 + 1]); maxy = fmaxf(maxy, loose[k][p * 2 + 1]);
    }
    layer_piece(&l, loose[k], nloose[k], bevel, rim, minx, miny, maxx - minx, maxy - miny);
  }
  uint8_t * small = layer_resolve(&l, BANNER_W, BANNER_H);
  layer_free(&l);
  uint8_t * out = malloc(BANNER_W * BANNER_H * 3);
  draw_felt(out, BANNER_W, 0, 0, BANNER_W, BANNER_H);
  composite(out, small, BANNER_W, BANNER_H);
  draw_frame(out, BANNER_W, BANNER_H, BANNER_FRAME);
  if(draw_plate(out)) return 1;
  int r = write_png(path, out, BANNER_W, BANNER_H, preview, BANNER_PREVIEW_SCALE);
  free(small);
  free(out);
  free(xy);
  free(ns);
  free(placed);
  return r;
}

int main(int argc, char ** argv) {
  if(argc >= 3 && !strcmp(argv[1], "icon")) return make_icon(argv[2], argc > 3 ? argv[3] : NULL);
  if(argc >= 3 && !strcmp(argv[1], "banner")) return make_banner(argv[2], argc > 3 ? argv[3] : NULL);
  fprintf(stderr, "usage: %s icon|banner out.png [preview.png]\n", argv[0]);
  return 1;
}

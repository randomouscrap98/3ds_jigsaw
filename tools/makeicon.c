// App icon generator: a bevelled gold puzzle piece on textured green felt.
//   make -f Makefile.host build_host/makeicon && ./build_host/makeicon icon.png [preview.png]
// The piece is rendered at ICON_SS times the size and averaged down; the felt is made at
// the final size so its texture survives. The preview is the icon scaled up (nearest).
#include "edgegen.h"
#include "imgscale.h"
#include "rng.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define ICON_SIZE 48
#define ICON_SS 8                       // Supersampling for the piece layer
#define ICON_BIG (ICON_SIZE * ICON_SS)
#define ICON_CELL 0.54f                 // Piece cell size, fraction of the icon
#define ICON_TAB 0.125f                 // Tab size (chunkier than the game's 0.1 so it reads at 48px)
#define ICON_NUDGE_X 0.02f              // Manual nudge after centering, fraction of the icon (+ = right)
#define ICON_NUDGE_Y 0.02f              // (+ = down)
#define ICON_BEVEL 0.11f                // Bevel width, fraction of the cell
#define ICON_SHADOW_OFF 0.035f          // Drop shadow offset (down-right), fraction of the icon
#define ICON_SHADOW_BLUR 0.03f          // Drop shadow softness, fraction of the icon
#define ICON_SHADOW_ALPHA 0.55f
#define ICON_PREVIEW_SCALE 6

static const float felt[3] = { 46, 64, 58 };   // Same as the game's green felt (render.c)
static const float gold[3] = { 212, 168, 58 };

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

int main(int argc, char ** argv) {
  if(argc < 2) {
    fprintf(stderr, "usage: %s icon.png [preview.png]\n", argv[0]);
    return 1;
  }
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
  const float cell = ICON_CELL * ICON_BIG;
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
  float sx = ICON_BIG * (0.5f + ICON_NUDGE_X) - cx, sy = ICON_BIG * (0.5f + ICON_NUDGE_Y) - cy;
  for(int i = 0; i < n; i++) {
    xy[i * 2] += sx;
    xy[i * 2 + 1] += sy;
  }

  // Height field: 0 at the piece edge rising to 1 one bevel width in, smoothstepped
  const int B = ICON_BIG;
  float * height = calloc((size_t)B * B, sizeof(float));
  float * cover = calloc((size_t)B * B, sizeof(float));
  const float bevel = ICON_BEVEL * cell;
  for(int y = 0; y < B; y++) {
    for(int x = 0; x < B; x++) {
      if(!inside(xy, n, x + 0.5f, y + 0.5f)) continue;
      float t = clampf(edgedist(xy, n, x + 0.5f, y + 0.5f) / bevel, 0, 1);
      height[y * B + x] = t * t * (3 - 2 * t);
      cover[y * B + x] = 1;
    }
  }
  // Shadow: blurred, offset coverage (separable box blur, 3 passes ~ gaussian)
  float * shadow = calloc((size_t)B * B, sizeof(float)), * tmp = calloc((size_t)B * B, sizeof(float));
  int off = (int)(ICON_SHADOW_OFF * B), rad = (int)(ICON_SHADOW_BLUR * B);
  for(int y = 0; y < B; y++) for(int x = 0; x < B; x++) {
    int ox = x - off, oy = y - off;
    shadow[y * B + x] = (ox >= 0 && oy >= 0) ? cover[oy * B + ox] : 0;
  }
  for(int pass = 0; pass < 3; pass++) {
    for(int y = 0; y < B; y++) for(int x = 0; x < B; x++) {
      float s = 0;
      for(int k = -rad; k <= rad; k++) s += shadow[y * B + (int)clampf(x + k, 0, B - 1)];
      tmp[y * B + x] = s / (2 * rad + 1);
    }
    for(int y = 0; y < B; y++) for(int x = 0; x < B; x++) {
      float s = 0;
      for(int k = -rad; k <= rad; k++) s += tmp[(int)clampf(y + k, 0, B - 1) * B + x];
      shadow[y * B + x] = s / (2 * rad + 1);
    }
  }

  // Shade the piece layer: premultiplied RGBA so averaging down is correct
  uint8_t * layer = malloc((size_t)B * B * 4);
  const float lx = -0.55f, ly = -0.65f, lz = 0.52f;   // Light from the top left
  float ll = sqrtf(lx * lx + ly * ly + lz * lz);
  float hx = lx / ll, hy = ly / ll, hz = lz / ll + 1;  // Half vector (viewer straight on)
  float hl = sqrtf(hx * hx + hy * hy + hz * hz);
  for(int y = 0; y < B; y++) {
    for(int x = 0; x < B; x++) {
      uint8_t * p = layer + ((size_t)y * B + x) * 4;
      float c = cover[y * B + x];
      if(c <= 0) {
        float a = shadow[y * B + x] * ICON_SHADOW_ALPHA;
        p[0] = p[1] = p[2] = 0;
        p[3] = (uint8_t)(a * 255 + 0.5f);
        continue;
      }
      float h = height[y * B + x];
      float gx = (height[y * B + (x + 1 < B ? x + 1 : x)] - height[y * B + (x > 0 ? x - 1 : x)]) / 2;
      float gy = (height[(y + 1 < B ? y + 1 : y) * B + x] - height[(y > 0 ? y - 1 : y) * B + x]) / 2;
      // Steeper bevel = bigger normal tilt
      float k = bevel * 0.9f;
      float nx = -gx * k, ny = -gy * k, nz = 1, nl = sqrtf(nx * nx + ny * ny + nz * nz);
      nx /= nl; ny /= nl; nz /= nl;
      float diff = clampf((nx * lx + ny * ly + nz * lz) / ll, 0, 1);
      float spec = powf(clampf((nx * hx + ny * hy + nz * hz) / hl, 0, 1), 28);
      // Faint brushed sheen across the flat top, brighter toward the top left
      float sheen = 0.08f * (1 - (x + y) / (2.0f * B));
      float shade = 0.35f + 0.85f * diff + sheen;
      float rgb[3];
      for(int i = 0; i < 3; i++) rgb[i] = clampf(gold[i] * shade + 255 * spec * (0.35f + 0.4f * (1 - h)), 0, 255);
      // Thin dark rim right at the edge for contrast against the felt
      float rim = clampf(edgedist(xy, n, x + 0.5f, y + 0.5f) / (ICON_SS * 0.6f), 0, 1);
      for(int i = 0; i < 3; i++) rgb[i] *= 0.45f + 0.55f * rim;
      p[0] = (uint8_t)rgb[0];
      p[1] = (uint8_t)rgb[1];
      p[2] = (uint8_t)rgb[2];
      p[3] = 255;
    }
  }
  uint8_t * small = malloc(ICON_SIZE * ICON_SIZE * 4);
  if(imgscale_area(layer, B, B, 4, 0, 0, B, B, small, ICON_SIZE, ICON_SIZE)) return 1;

  // Felt at final size: base color, soft vignette, blotchy low-frequency + fine grain
  uint8_t * out = malloc(ICON_SIZE * ICON_SIZE * 3);
  Rng r;
  rng_init(&r, 12345);
  for(int y = 0; y < ICON_SIZE; y++) {
    for(int x = 0; x < ICON_SIZE; x++) {
      float u = (x + 0.5f) / ICON_SIZE - 0.5f, v = (y + 0.5f) / ICON_SIZE - 0.5f;
      float vig = 1.12f - 0.55f * (u * u + v * v);
      float blotch = (vnoise(x / 6.0f, y / 6.0f, 7) - 0.5f) * 0.10f;
      float grain = (rng_float(&r) - 0.5f) * 0.12f;
      float m = vig + blotch + grain;
      const uint8_t * p = small + (y * ICON_SIZE + x) * 4;
      float a = p[3] / 255.0f;
      for(int i = 0; i < 3; i++) {
        float bg = clampf(felt[i] * m, 0, 255);
        out[(y * ICON_SIZE + x) * 3 + i] = (uint8_t)clampf(p[i] + bg * (1 - a) + 0.5f, 0, 255);
      }
    }
  }
  stbi_write_png_compression_level = 9;
  int ok = stbi_write_png(argv[1], ICON_SIZE, ICON_SIZE, 3, out, ICON_SIZE * 3);
  if(ok && argc > 2) {
    const int S = ICON_PREVIEW_SCALE, P = ICON_SIZE * S;
    uint8_t * big = malloc((size_t)P * P * 3);
    for(int y = 0; y < P; y++) for(int x = 0; x < P; x++) {
      memcpy(big + ((size_t)y * P + x) * 3, out + ((y / S) * ICON_SIZE + x / S) * 3, 3);
    }
    ok = stbi_write_png(argv[2], P, P, 3, big, P * 3);
    free(big);
  }
  free(height); free(cover); free(shadow); free(tmp); free(layer); free(small); free(out);
  return ok ? 0 : 1;
}

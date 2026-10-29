#include "knobs.h"

#include <math.h>

// Thick spiral finger standing on (0.5 + shift, 0), curling one way or the other
static int knob_curl(const EdgeTab * tab, float * out) {
  float sc = tab->size / 0.1f, shift = tab->b;
  float stem = KNOB_CURL_STEM * sc, len = stem + KNOB_CURL_LEN * sc, hw = KNOB_CURL_WIDTH * sc / 2;
  float dir = tab->a < 0 ? -1.0f : 1.0f;   // Curl left or right
  // Centerline (x, y, heading)
  float cx[KNOB_CURL_SEG + 1], cy[KNOB_CURL_SEG + 1], ch[KNOB_CURL_SEG + 1];
  float x = 0.5f + shift, y = 0, h = (float)M_PI / 2, ds = len / KNOB_CURL_SEG;
  for(int i = 0; i <= KNOB_CURL_SEG; i++) {
    cx[i] = x; cy[i] = y; ch[i] = h;
    float along = (i + 0.5f) * ds - stem, f = along / (len - stem);
    float k = along < 0 ? 0 : (KNOB_CURL_K0 + (KNOB_CURL_K1 - KNOB_CURL_K0) * f) / sc;
    // Midpoint step
    float hm = h + dir * k * ds / 2;
    x += cosf(hm) * ds;
    y += sinf(hm) * ds;
    h += dir * k * ds;
  }
  int n = 0;
#define CURL_PT(px, py) do { out[n * 2] = (px); out[n * 2 + 1] = (py) * tab->flip; n++; } while(0)
  CURL_PT(0, 0);
  // Side walked first: the outside of the curl when curling right (dir > 0 turns left in
  // y-up space, so the right side is outside), so the outline goes left to right along t
  for(int pass = 0; pass < 2; pass++) {
    // pass 0: up the left side (+90° from heading), pass 1: down the right side
    for(int j = 0; j <= KNOB_CURL_SEG; j++) {
      int i = pass == 0 ? j : KNOB_CURL_SEG - j;
      float f = (float)i / KNOB_CURL_SEG;
      float w = hw * (1 + KNOB_CURL_FLARE * expf(-f * 14));
      float side = pass == 0 ? 1.0f : -1.0f;
      float nx = -sinf(ch[i]) * side, ny = cosf(ch[i]) * side;
      CURL_PT(cx[i] + nx * w, i == 0 ? 0 : cy[i] + ny * w);
      if(pass == 0 && i == KNOB_CURL_SEG) {
        // Round tip: half circle from the left side to the right side, ahead of the end
        for(int a = 1; a < 6; a++) {
          float ang = ch[i] + (float)M_PI / 2 - (float)M_PI * a / 6;
          CURL_PT(cx[i] + cosf(ang) * hw, cy[i] + sinf(ang) * hw);
        }
      }
    }
  }
  CURL_PT(1, 0);
#undef CURL_PT
  return n;
}

// Double spiral standing on the line: up a leg into one arm, in to the centre, out along the
// other arm (Archimedean arms half a turn apart: the gap between them is the finger width),
// then an arch over and down to the line. Built turning one way, mirrored for the other.
static int knob_swirl(const EdgeTab * tab, float * out) {
  float w = KNOB_SWIRL_W, b = w / (float)M_PI, pm = 2 * (float)M_PI * KNOB_SWIRL_TURNS;
  float rout = b * pm, cx = KNOB_SWIRL_MARGIN + rout, cy = rout + KNOB_SWIRL_LIFT * w;
  float th0 = (float)M_PI - pm;
  int steps = (int)(KNOB_SWIRL_STEPS * KNOB_SWIRL_TURNS);
  int n = 0;
#define SW_PT(px, py) do { out[n * 2] = (px); out[n * 2 + 1] = (py); n++; } while(0)
#define SW_ARM(phi, sgn, X, Y) do { float _r = b * (phi) * (sgn); X = cx + _r * cosf((phi) + th0); Y = cy + _r * sinf((phi) + th0); } while(0)
  float ax, ay, ax2, ay2, bx, by, bx2, by2, d = 0.01f;
  SW_ARM(pm, 1, ax, ay);          // Arm A's outer end (left), walked inward
  SW_ARM(pm - d, 1, ax2, ay2);
  SW_ARM(pm, -1, bx, by);         // Arm B's outer end (right), walked outward
  SW_ARM(pm - d, -1, bx2, by2);
  float tax = ax2 - ax, tay = ay2 - ay, tl = hypotf(tax, tay);
  tax /= tl; tay /= tl;
  float tbx = bx - bx2, tby = by - by2;
  tl = hypotf(tbx, tby);
  tbx /= tl; tby /= tl;
  float k = cy * 0.55f;
  SW_PT(0, 0);
  // Leg straight up into arm A
  for(int i = 0; i <= 8; i++) {
    float t = i / 8.0f, u = 1 - t, x0 = ax, c1y = k, c2x = ax - tax * k, c2y = ay - tay * k;
    SW_PT(u * u * u * x0 + 3 * u * u * t * x0 + 3 * u * t * t * c2x + t * t * t * ax,
        3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * ay);
  }
  for(int i = 1; i <= steps; i++) { float X, Y; SW_ARM(pm * (1 - (float)i / steps), 1, X, Y); SW_PT(X, Y); }
  for(int i = 1; i <= steps; i++) { float X, Y; SW_ARM(pm * (float)i / steps, -1, X, Y); SW_PT(X, Y); }
  // Arch: on along arm B's heading, over and down to the line
  float qx = bx + KNOB_SWIRL_ARCH * w;
  for(int i = 1; i <= 10; i++) {
    float t = i / 10.0f, u = 1 - t;
    float c1x = bx + tbx * k, c1y = by + tby * k, c2x = qx, c2y = by + tby * k;
    SW_PT(u * u * u * bx + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * qx,
        u * u * u * by + 3 * u * u * t * c1y + 3 * u * t * t * c2y);
  }
  SW_PT(1, 0);
#undef SW_ARM
#undef SW_PT
  if(tab->a < 0) {
    // Turning the other way: mirrored along the line, walked from the other end
    for(int i = 0; i < n; i++) out[i * 2] = 1 - out[i * 2];
    for(int i = 0; i < n / 2; i++) {
      float tx = out[i * 2], ty = out[i * 2 + 1];
      out[i * 2] = out[(n - 1 - i) * 2]; out[i * 2 + 1] = out[(n - 1 - i) * 2 + 1];
      out[(n - 1 - i) * 2] = tx; out[(n - 1 - i) * 2 + 1] = ty;
    }
  }
  for(int i = 0; i < n; i++) out[i * 2 + 1] *= tab->flip;
  return n;
}

// Circle standing in the line (no neck), entered and left through small fillets
static int knob_circle(const EdgeTab * tab, float * out) {
  float R = KNOB_CIRCLE_R * tab->size, h = KNOB_CIRCLE_SINK * R, cx = 0.5f + tab->b * 0.5f;
  float ow = sqrtf(R * R - h * h), al = atan2f(h, ow), fr = KNOB_CIRCLE_FILLET, dl = 0.2f;
  // Arc from just past the left crossing, over the top, to just before the right one (y up,
  // angles decreasing = clockwise)
  float p0 = (float)M_PI + al - dl, p1 = -al + dl;
  int n = 0, arcn = 18;
#define CI_PT(px, py) do { out[n * 2] = (px); out[n * 2 + 1] = (py) * tab->flip; n++; } while(0)
  CI_PT(0, 0);
  float lx = cx - ow - fr, sx = cx + R * cosf(p0), sy = h + R * sinf(p0);
  float tx = sinf(p0), ty = -cosf(p0);   // Travel direction at p0 (clockwise)
  float k = fr * 0.8f;
  for(int i = 0; i <= 6; i++) {
    float t = i / 6.0f, u = 1 - t;
    float c1x = lx + k, c1y = 0, c2x = sx - tx * k, c2y = sy - ty * k;
    CI_PT(u * u * u * lx + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * sx,
        3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * sy);
  }
  for(int i = 1; i < arcn; i++) {
    float a = p0 + (p1 - p0) * i / arcn;
    CI_PT(cx + R * cosf(a), h + R * sinf(a));
  }
  float ex = cx + R * cosf(p1), ey = h + R * sinf(p1), rx = cx + ow + fr;
  tx = sinf(p1); ty = -cosf(p1);
  for(int i = 0; i <= 6; i++) {
    float t = i / 6.0f, u = 1 - t;
    float c1x = ex + tx * k, c1y = ey + ty * k, c2x = rx - k, c2y = 0;
    CI_PT(u * u * u * ex + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * rx,
        u * u * u * ey + 3 * u * u * t * c1y + 3 * u * t * t * c2y);
  }
  CI_PT(1, 0);
#undef CI_PT
  return n;
}

int knob_curve(uint8_t kind, const EdgeTab * tab, float * out) {
  if(kind == CUTKNOB_CIRCLE) return knob_circle(tab, out);
  if(kind == CUTKNOB_SWIRL) return knob_swirl(tab, out);
  if(kind == CUTKNOB_CURL) return knob_curl(tab, out);
  if(kind == CUTKNOB_CLASSIC || kind >= CUTKNOBS) return edgegen_tabcurve(tab, out);
  // Same frame as edgegen's tab: neck at p3 / p6, head between them through p4, p5. t is
  // the tab size (~0.1), the rest small jitters.
  float t = tab->size, b = tab->b, c = tab->c, d = tab->d;
  // Smooth shoulders: no dip below the line and no wobble where the knob leaves it
  float p[10][2] = {
    { 0.0f, 0.0f },
    { 0.25f, 0.0f },
    { 0.5f + b + d, 0.0f },
    { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
    { 0.5f + b + d, 0.0f },
    { 0.75f, 0.0f },
    { 1.0f, 0.0f },
  };
  // Neck half width, neck height, head half width, head height (in t)
  // These are for wood-like cuts: necks stand high enough that the neighbour's material under
  // the head isn't a sliver
  float nw = 1, nh = 1.7f, hw = 2.1f, hh = 3.9f, lean = 0;
  switch(kind) {
    case CUTKNOB_BALL: nw = 0.9f; nh = 1.8f; hw = 2.7f; hh = 4.8f; break;
    case CUTKNOB_HOOK: nw = 1.0f; nh = 1.7f; hw = 2.0f; hh = 4.4f; lean = tab->a < 0 ? -1.0f : 1.0f; break;
    case CUTKNOB_BLOCK: nw = 1.5f; nh = 1.5f; hw = 2.5f; hh = 3.8f; break;
    case CUTKNOB_FINGER: nw = 0.9f; nh = 2.2f; hw = 1.6f; hh = 5.8f; break;
  }
  p[3][0] = 0.5f - nw * t + b;            p[3][1] = nh * t + c;
  p[4][0] = 0.5f + (lean - hw) * t + b - d; p[4][1] = hh * t + c;
  p[5][0] = 0.5f + (lean + hw) * t + b - d; p[5][1] = hh * t + c;
  p[6][0] = 0.5f + nw * t + b;            p[6][1] = nh * t + c;
  return edgegen_curve((const float (*)[2])p, tab->flip, out);
}

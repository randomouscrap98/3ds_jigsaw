#include "pottscut.h"
#include "geom.h"
#include "rng.h"
#include "whimsy.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

static const PottsStyle pottscut_styles[POTTSCUT_STYLES] = {
  // Picked by the user from sweeps (ignore/potts/noise_t8/e_cool_smooth12.png): very snaky,
  // annealed and smoothed so the lines aren't fuzzy
  { .res = 10, .sweeps = 100, .temp = 8, .adhesion = 1, .larea = 0.1f, .lperim = 0.05f, .perim = 2.6f, .smooth = 12,
    .cool = 0.3f },
};

const PottsStyle * pottscut_style(uint8_t style) {
  return pottscut_styles + (style < POTTSCUT_STYLES ? style : POTTSCUT_STYLE_DEFAULT);
}

// ---- The grid ----

typedef struct {
  int w, h;             // Squares
  int32_t * lab;        // Piece per square
  uint8_t * frozen;     // Whimsy squares (never change)
  int32_t * area, * perim;
  uint32_t count, nlab; // Pieces, pieces + whimsies
  float a0, p0;         // Target area and perimeter (squares, square sides)
  Rng r;
} Pt;

static int32_t pt_lab(const Pt * t, int x, int y) {
  return x < 0 || y < 0 || x >= t->w || y >= t->h ? -1 : t->lab[y * t->w + x];
}

// Ring of 8 neighbours, counter-clockwise from the right (y down: "up" is y - 1)
static const int pt_rx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, pt_ry[8] = { 0, -1, -1, -1, 0, 1, 1, 1 };

// Yokoi's connectivity number for label l around (x, y), 4-connected: 1 = (x, y) is a simple
// point of l's region (adding or removing it doesn't change its topology)
static int pt_yokoi(const Pt * t, int x, int y, int32_t l) {
  int b[9];
  for(int k = 0; k < 8; k++) b[k] = pt_lab(t, x + pt_rx[k], y + pt_ry[k]) == l;
  b[8] = b[0];
  int c = 0;
  for(int k = 0; k < 8; k += 2) c += b[k] - b[k] * b[k + 1] * b[(k + 2) % 8];
  return c;
}

// 1 if a 2x2 block containing (x, y) is a checkerboard (two pieces touching only at a corner)
static int pt_checker(const Pt * t, int x, int y) {
  for(int dy = -1; dy <= 1; dy += 2) {
    for(int dx = -1; dx <= 1; dx += 2) {
      int32_t a = pt_lab(t, x, y), b = pt_lab(t, x + dx, y), c = pt_lab(t, x, y + dy), d = pt_lab(t, x + dx, y + dy);
      if(a == d && b == c && a != b) return 1;
    }
  }
  return 0;
}

static void pt_measure(Pt * t) {
  memset(t->area, 0, sizeof(int32_t) * t->nlab);
  memset(t->perim, 0, sizeof(int32_t) * t->nlab);
  for(int y = 0; y < t->h; y++) {
    for(int x = 0; x < t->w; x++) {
      int32_t l = t->lab[y * t->w + x];
      t->area[l]++;
      t->perim[l] += (pt_lab(t, x + 1, y) != l) + (pt_lab(t, x - 1, y) != l) + (pt_lab(t, x, y + 1) != l) + (pt_lab(t, x, y - 1) != l);
    }
  }
}

// ---- Start: whimsies, then even blobs (Lloyd-relaxed Voronoi on the grid) ----

// Whimsy shapes rasterized as frozen labels count .. count + n - 1. Returns how many.
static uint32_t pt_whimsies(Pt * t, uint32_t want, uint8_t set, float res) {
  float base[WHIMSY_MAXSHAPE * 2], shape[WHIMSY_MAXSHAPE * 2];
  float placed[64 * 3];
  uint32_t n = 0, nshapes = whimsy_setsize(set), first = rng_below(&t->r, nshapes);
  float W = t->w / res, H = t->h / res;
  for(uint32_t k = 0; k < want && n < 64; k++) {
    uint32_t m = whimsy_shape(set, first + k, base);
    float rad = 0;
    for(uint32_t i = 0; i < m; i++) rad = fmaxf(rad, hypotf(base[2 * i], base[2 * i + 1]));
    for(int tr = 0; tr < WHIMSY_TRIES; tr++) {
      float sc = sqrtf(WHIMSY_AREA * rng_range(&t->r, 0.85f, 1.15f));
      float ang = rng_range(&t->r, -0.35f, 0.35f), ca = cosf(ang), sa = sinf(ang);
      float R = rad * sc, lo = WHIMSY_BORDER + R;
      if(W - 2 * lo <= 0 || H - 2 * lo <= 0) break;
      float cx = rng_range(&t->r, lo, W - lo), cy = rng_range(&t->r, lo, H - lo);
      int close = 0;
      for(uint32_t j = 0; j < n; j++) {
        if(hypotf(cx - placed[3 * j], cy - placed[3 * j + 1]) < R + placed[3 * j + 2] + 2 * WHIMSY_GAP) close = 1;
      }
      if(close) continue;
      for(uint32_t i = 0; i < m; i++) {
        float px = base[2 * i] * sc, py = base[2 * i + 1] * sc;
        shape[2 * i] = cx + px * ca - py * sa;
        shape[2 * i + 1] = cy + px * sa + py * ca;
      }
      int32_t l = t->count + n;
      for(int y = 0; y < t->h; y++) {
        for(int x = 0; x < t->w; x++) {
          if(geom_inside(shape, m, (x + 0.5f) / res, (y + 0.5f) / res)) {
            t->lab[y * t->w + x] = l;
            t->frozen[y * t->w + x] = 1;
          }
        }
      }
      placed[3 * n] = cx;
      placed[3 * n + 1] = cy;
      placed[3 * n + 2] = R;
      n++;
      break;
    }
  }
  return n;
}

// Each piece keeps only its largest 4-connected part; the rest goes to neighbouring pieces.
// queue, comp: scratch, one int per square. Returns 0 on success (1 if a piece has nothing).
static int pt_keeplargest(Pt * t, int * queue, int32_t * comp) {
  uint32_t count = t->count;
  int n = t->w * t->h, err = 0;
  {
    int32_t * best = malloc(sizeof(int32_t) * count), * bestsize = calloc(count, sizeof(int32_t));
    if(!best || !bestsize) err = 1;
    for(int i = 0; i < n && !err; i++) comp[i] = -1;
    int ncomp = 0;
    int32_t * csize = NULL;
    for(int i = 0; i < n && !err; i++) {
      if(comp[i] >= 0 || t->frozen[i]) continue;
      int32_t l = t->lab[i];
      int qh = 0, qt = 0, size = 0;
      queue[qt++] = i;
      comp[i] = ncomp;
      while(qh < qt) {
        int c = queue[qh++], x = c % t->w, y = c / t->w;
        size++;
        int nb[4] = { x > 0 ? c - 1 : -1, x < t->w - 1 ? c + 1 : -1, y > 0 ? c - t->w : -1, y < t->h - 1 ? c + t->w : -1 };
        for(int k = 0; k < 4; k++) {
          if(nb[k] >= 0 && comp[nb[k]] < 0 && !t->frozen[nb[k]] && t->lab[nb[k]] == l) {
            comp[nb[k]] = ncomp;
            queue[qt++] = nb[k];
          }
        }
      }
      int32_t * cs = realloc(csize, sizeof(int32_t) * (ncomp + 1));
      if(!cs) { err = 1; break; }
      csize = cs;
      csize[ncomp] = size;
      if(size > bestsize[l]) { bestsize[l] = size; best[l] = ncomp; }
      ncomp++;
    }
    for(uint32_t l = 0; l < count && !err; l++) if(!bestsize[l]) err = 1;   // A piece got nothing
    // Squares in a piece's other parts go to a neighbouring piece's main part, repeatedly
    for(int i = 0; i < n && !err; i++) if(!t->frozen[i] && comp[i] != best[t->lab[i]]) t->lab[i] = -2;
    int left = 1;
    while(left && !err) {
      left = 0;
      int changed = 0;
      for(int i = 0; i < n; i++) {
        if(t->lab[i] != -2) continue;
        int x = i % t->w, y = i / t->w;
        int32_t nb[4] = { pt_lab(t, x - 1, y), pt_lab(t, x + 1, y), pt_lab(t, x, y - 1), pt_lab(t, x, y + 1) };
        int32_t take = -1;
        for(int k = 0; k < 4; k++) if(nb[k] >= 0 && (uint32_t)nb[k] < count) take = nb[k];
        if(take >= 0) { t->lab[i] = take; changed = 1; }
        else left = 1;
      }
      if(left && !changed) err = 1;
    }
    free(best);
    free(bestsize);
    free(csize);
    }
  return err;
}

// Even blobs: sites relaxed a few times, every free square to its nearest site; then each
// piece keeps only its largest 4-connected part (the rest goes to neighbours). Returns 0 on success.
static int pt_blobs(Pt * t, float res) {
  uint32_t count = t->count;
  int n = t->w * t->h;
  float * sx = malloc(sizeof(float) * count), * sy = malloc(sizeof(float) * count);
  double * ax = malloc(sizeof(double) * count), * ay = malloc(sizeof(double) * count);
  int32_t * an = malloc(sizeof(int32_t) * count);
  int * queue = malloc(sizeof(int) * n);
  int32_t * comp = malloc(sizeof(int32_t) * n);
  int err = !sx || !sy || !ax || !ay || !an || !queue || !comp;
  for(uint32_t i = 0; i < count && !err; i++) {
    // Not on a whimsy
    int tries = 0;
    do {
      sx[i] = rng_range(&t->r, 0, t->w);
      sy[i] = rng_range(&t->r, 0, t->h);
    } while(t->frozen[(int)sy[i] * t->w + (int)sx[i]] && ++tries < 100);
  }
  for(int pass = 0; pass <= 4 && !err; pass++) {
    // Brute force nearest site, bucketed by rows of sites would be faster; the grid is small
    // enough that a coarse bucket grid of res-sized squares does it
    int bw = (t->w + (int)res - 1) / (int)res, bh = (t->h + (int)res - 1) / (int)res;
    int32_t * bstart = calloc(bw * bh + 1, sizeof(int32_t)), * bitems = malloc(sizeof(int32_t) * count);
    if(!bstart || !bitems) { free(bstart); free(bitems); err = 1; break; }
    for(uint32_t i = 0; i < count; i++) {
      int bx = (int)(sx[i] / res), by = (int)(sy[i] / res);
      if(bx >= bw) bx = bw - 1;
      if(by >= bh) by = bh - 1;
      bstart[by * bw + bx + 1]++;
    }
    for(int b = 0; b < bw * bh; b++) bstart[b + 1] += bstart[b];
    int32_t * fill = malloc(sizeof(int32_t) * bw * bh);
    if(!fill) { free(bstart); free(bitems); err = 1; break; }
    memcpy(fill, bstart, sizeof(int32_t) * bw * bh);
    for(uint32_t i = 0; i < count; i++) {
      int bx = (int)(sx[i] / res), by = (int)(sy[i] / res);
      if(bx >= bw) bx = bw - 1;
      if(by >= bh) by = bh - 1;
      bitems[fill[by * bw + bx]++] = i;
    }
    free(fill);
    memset(ax, 0, sizeof(double) * count);
    memset(ay, 0, sizeof(double) * count);
    memset(an, 0, sizeof(int32_t) * count);
    for(int y = 0; y < t->h; y++) {
      for(int x = 0; x < t->w; x++) {
        if(t->frozen[y * t->w + x]) continue;
        float px = x + 0.5f, py = y + 0.5f, best = 1e30f;
        int32_t bi = 0;
        int cbx = (int)(px / res), cby = (int)(py / res);
        for(int r = 1; r < 64; r++) {
          for(int by = cby - r; by <= cby + r; by++) {
            for(int bx = cbx - r; bx <= cbx + r; bx++) {
              if(bx < 0 || by < 0 || bx >= bw || by >= bh) continue;
              for(int32_t k = bstart[by * bw + bx]; k < bstart[by * bw + bx + 1]; k++) {
                int32_t i = bitems[k];
                float d = (sx[i] - px) * (sx[i] - px) + (sy[i] - py) * (sy[i] - py);
                if(d < best) { best = d; bi = i; }
              }
            }
          }
          // Anything further than this ring can't be closer
          if(best < (r * res) * (r * res)) break;
        }
        t->lab[y * t->w + x] = bi;
        ax[bi] += px; ay[bi] += py; an[bi]++;
      }
    }
    free(bstart);
    free(bitems);
    if(pass < 4) {
      for(uint32_t i = 0; i < count; i++) {
        if(an[i]) { sx[i] = ax[i] / an[i]; sy[i] = ay[i] / an[i]; }
      }
    }
  }
  if(!err) err = pt_keeplargest(t, queue, comp);
  free(sx); free(sy); free(ax); free(ay); free(an); free(queue); free(comp);
  return err;
}

// Turn checkerboards (pieces touching only at a corner) into proper contacts where it keeps
// both pieces' shapes simple. Returns the number left.
static int pt_unchecker(Pt * t) {
  int left = 0;
  for(int y = 0; y < t->h; y++) {
    for(int x = 0; x < t->w; x++) {
      if(!pt_checker(t, x, y)) continue;
      int i = y * t->w + x, fixed = 0;
      int32_t l = t->lab[i];
      for(int k = 0; k < 8 && !fixed; k += 2) {
        int32_t m = pt_lab(t, x + pt_rx[k], y + pt_ry[k]);
        if(m < 0 || m == l || t->frozen[i] || (uint32_t)m >= t->count) continue;
        if(pt_yokoi(t, x, y, l) != 1 || pt_yokoi(t, x, y, m) != 1) continue;
        t->lab[i] = m;
        if(pt_checker(t, x, y)) t->lab[i] = l;
        else fixed = 1;
      }
      left += !fixed;
    }
  }
  return left;
}

// ---- The automaton ----

static void pt_run(Pt * t, const PottsStyle * st) {
  long props = (long)(st->sweeps * t->w * t->h);
  int minarea = (int)(POTTSCUT_MINAREA * t->a0);
  const int dx4[4] = { 1, -1, 0, 0 }, dy4[4] = { 0, 0, 1, -1 };
  long coolfrom = (long)(props * (1 - st->cool));
  float temp = st->temp;
  for(long p = 0; p < props; p++) {
    if(p >= coolfrom && (p & 1023) == 0) {
      float f = (float)(p - coolfrom) / (props - coolfrom + 1);
      temp = st->temp * (1 - f * (1 - POTTSCUT_COOLTO));
    }
    int x = rng_below(&t->r, t->w), y = rng_below(&t->r, t->h), i = y * t->w + x;
    if(t->frozen[i]) continue;
    int d = rng_below(&t->r, 4);
    int32_t l = t->lab[i], m = pt_lab(t, x + dx4[d], y + dy4[d]);
    if(m < 0 || m == l || (uint32_t)m >= t->count || t->area[l] <= minarea) continue;
    // Both pieces stay one simple shape (no split, no hole)
    if(pt_yokoi(t, x, y, l) != 1 || pt_yokoi(t, x, y, m) != 1) continue;
    int nl = 0, nm = 0, before = 0, after = 0;
    for(int k = 0; k < 4; k++) {
      int32_t q = pt_lab(t, x + dx4[k], y + dy4[k]);
      nl += q == l;
      nm += q == m;
    }
    for(int k = 0; k < 8; k++) {
      int32_t q = pt_lab(t, x + pt_rx[k], y + pt_ry[k]);
      before += q != l;
      after += q != m;
    }
    float al = t->area[l], am = t->area[m], pl = t->perim[l], pm = t->perim[m];
    float dpl = 2 * nl - 4, dpm = 4 - 2 * nm;
    float dh = st->adhesion * (after - before);
    dh += st->larea * ((al - 1 - t->a0) * (al - 1 - t->a0) - (al - t->a0) * (al - t->a0) +
        (am + 1 - t->a0) * (am + 1 - t->a0) - (am - t->a0) * (am - t->a0));
    dh += st->lperim * ((pl + dpl - t->p0) * (pl + dpl - t->p0) - (pl - t->p0) * (pl - t->p0) +
        (pm + dpm - t->p0) * (pm + dpm - t->p0) - (pm - t->p0) * (pm - t->p0));
    if(dh > 0 && rng_range(&t->r, 0, 1) >= expf(-dh / temp)) continue;
    t->lab[i] = m;
    if(pt_checker(t, x, y)) {
      t->lab[i] = l;
      continue;
    }
    t->area[l]--;
    t->area[m]++;
    t->perim[l] += (int)dpl;
    t->perim[m] += (int)dpm;
  }
}

// ---- Lines ----

typedef struct {
  int32_t from, to;     // Junction lattice points
  int32_t left, right;  // Pieces on the left / right walking from -> to (-1 = outside)
  uint32_t start, len;  // Interior points: cpts[start .. start + len) (x, y in cells)
} PtChain;

static int pt_tograph(Pt * t, CutGraph * g, float res, uint8_t smooth) {
  int W = t->w, H = t->h, lw = W + 1, nlat = lw * (H + 1);
  // Boundary sides: h[y * W + x] between squares (x, y - 1) and (x, y); v[y * lw + x] between (x - 1, y) and (x, y)
  uint8_t * hs = calloc((size_t)W * (H + 1), 1), * vs = calloc((size_t)lw * H, 1);
  int32_t * vmap = malloc(sizeof(int32_t) * nlat);
  PtChain * ch = NULL;
  float * cp = NULL;
  uint32_t nch = 0, capch = 0, ncp = 0, capcp = 0;
  int err = !hs || !vs || !vmap;
  int nv = 0;
  for(int y = 0; y <= H && !err; y++) {
    for(int x = 0; x <= W; x++) {
      int32_t q[4] = { pt_lab(t, x - 1, y - 1), pt_lab(t, x, y - 1), pt_lab(t, x - 1, y), pt_lab(t, x, y) };
      int distinct = 1;
      for(int a = 1; a < 4; a++) {
        int seen = 0;
        for(int b = 0; b < a; b++) seen |= q[a] == q[b];
        distinct += !seen;
      }
      int corner = (x == 0 || x == W) && (y == 0 || y == H);
      vmap[y * lw + x] = distinct >= 3 || corner ? nv++ : -1;
    }
  }
  // Chains: from each junction along each boundary side not yet walked, to the next junction
  const int ddx[4] = { 1, 0, -1, 0 }, ddy[4] = { 0, 1, 0, -1 };
#define PT_SIDES(x, y, d, s1, s2) do { \
    if(d == 0) { s1 = pt_lab(t, x, y); s2 = pt_lab(t, x, y - 1); } \
    else if(d == 1) { s1 = pt_lab(t, x - 1, y); s2 = pt_lab(t, x, y); } \
    else if(d == 2) { s1 = pt_lab(t, x - 1, y - 1); s2 = pt_lab(t, x - 1, y); } \
    else { s1 = pt_lab(t, x, y - 1); s2 = pt_lab(t, x - 1, y - 1); } } while(0)
#define PT_SEEN(x, y, d) (d == 0 ? &hs[(y) * W + (x)] : d == 2 ? &hs[(y) * W + (x) - 1] : d == 1 ? &vs[(y) * lw + (x)] : &vs[((y) - 1) * lw + (x)])
  for(int y = 0; y <= H && !err; y++) {
    for(int x = 0; x <= W && !err; x++) {
      if(vmap[y * lw + x] < 0) continue;
      for(int d0 = 0; d0 < 4 && !err; d0++) {
        int nx = x + ddx[d0], ny = y + ddy[d0];
        if(nx < 0 || ny < 0 || nx > W || ny > H) continue;
        int32_t s1, s2;
        PT_SIDES(x, y, d0, s1, s2);
        if(s1 == s2 || *PT_SEEN(x, y, d0)) continue;
        if(nch == capch) {
          capch = capch ? capch * 2 : 256;
          PtChain * nc = realloc(ch, sizeof(PtChain) * capch);
          if(!nc) { err = 1; break; }
          ch = nc;
        }
        PtChain * c = ch + nch++;
        c->from = y * lw + x;
        c->left = s1 < 0 ? -1 : s1;
        c->right = s2 < 0 ? -1 : s2;
        c->start = ncp;
        int cx = x, cy = y, d = d0;
        for(;;) {
          *PT_SEEN(cx, cy, d) = 1;
          cx += ddx[d];
          cy += ddy[d];
          if(vmap[cy * lw + cx] >= 0) break;
          if(ncp == capcp) {
            capcp = capcp ? capcp * 2 : 4096;
            float * np = realloc(cp, sizeof(float) * 2 * capcp);
            if(!np) { err = 1; break; }
            cp = np;
          }
          cp[2 * ncp] = cx / res;
          cp[2 * ncp + 1] = cy / res;
          ncp++;
          // On along the other boundary side here (exactly one, away from where we came from)
          int nd = -1;
          for(int k = 0; k < 4; k++) {
            if(k == (d + 2) % 4) continue;
            int ex = cx + ddx[k], ey = cy + ddy[k];
            if(ex < 0 || ey < 0 || ex > W || ey > H) continue;
            int32_t a, b;
            PT_SIDES(cx, cy, k, a, b);
            if(a != b) { nd = k; break; }
          }
          if(nd < 0) { err = 1; break; }
          d = nd;
        }
        c->to = cy * lw + cx;
        c->len = ncp - c->start;
        if(c->from == c->to) err = 1;   // A loop back to the same point: not a proper edge
      }
    }
  }
  // Anything not walked is a closed loop with no junction (a piece inside another)
  for(int i = 0; i < W * (H + 1) && !err; i++) {
    int x = i % W, y = i / W;
    if(pt_lab(t, x, y - 1) != pt_lab(t, x, y) && !hs[i]) err = 1;
  }
  for(int i = 0; i < lw * H && !err; i++) {
    int x = i % lw, y = i / lw;
    if(pt_lab(t, x - 1, y) != pt_lab(t, x, y) && !vs[i]) err = 1;
  }
  // Smooth the inner lines (ends pinned): Laplacian passes
  float * tmp = err ? NULL : malloc(sizeof(float) * 2 * (ncp ? ncp : 1));
  if(!err && !tmp) err = 1;
  for(uint32_t k = 0; k < nch && !err; k++) {
    PtChain * c = ch + k;
    if(c->left < 0 || c->right < 0 || c->len == 0) continue;
    float ax = (c->from % lw) / res, ay = (c->from / lw) / res, bx = (c->to % lw) / res, by = (c->to / lw) / res;
    for(int pass = 0; pass < smooth; pass++) {
      float * p = cp + 2 * c->start;
      for(uint32_t i = 0; i < c->len; i++) {
        float px = i ? p[2 * i - 2] : ax, py = i ? p[2 * i - 1] : ay;
        float qx = i + 1 < c->len ? p[2 * i + 2] : bx, qy = i + 1 < c->len ? p[2 * i + 3] : by;
        tmp[2 * i] = 0.5f * p[2 * i] + 0.25f * (px + qx);
        tmp[2 * i + 1] = 0.5f * p[2 * i + 1] + 0.25f * (py + qy);
      }
      memcpy(p, tmp, sizeof(float) * 2 * c->len);
    }
  }
  free(tmp);
  // The cut graph
  if(!err) {
    memset(g, 0, sizeof(CutGraph));
    g->count = t->nlab;
    g->width = W / res;
    g->height = H / res;
    g->v = malloc(sizeof(CutVert) * (nv ? nv : 1));
    g->e = calloc(nch ? nch : 1, sizeof(CutEdge));
    g->pts = malloc(sizeof(float) * 2 * (ncp ? ncp : 1));
    err = !g->v || !g->e || !g->pts;
  }
  if(!err) {
    g->nv = nv;
    for(int i = 0; i < nlat; i++) {
      if(vmap[i] >= 0) { g->v[vmap[i]].x = (i % lw) / res; g->v[vmap[i]].y = (i / lw) / res; }
    }
    for(uint32_t k = 0; k < nch; k++) {
      const PtChain * c = ch + k;
      CutEdge * e = g->e + g->ne++;
      int32_t va = vmap[c->from], vb = vmap[c->to];
      int fwd = va < vb;
      e->a = fwd ? va : vb;
      e->b = fwd ? vb : va;
      e->left = fwd ? c->left : c->right;
      e->right = fwd ? c->right : c->left;
      e->p0 = g->npts;
      e->npts = c->len;
      for(uint32_t i = 0; i < c->len; i++) {
        uint32_t s = c->start + (fwd ? i : c->len - 1 - i);
        g->pts[2 * g->npts] = cp[2 * s];
        g->pts[2 * g->npts + 1] = cp[2 * s + 1];
        g->npts++;
      }
      if((e->left >= 0 && (uint32_t)e->left >= t->count) || (e->right >= 0 && (uint32_t)e->right >= t->count)) {
        e->flags |= CUTEDGE_WHIMSY;
      }
    }
    if(cutgraph_rewalk(g)) err = 1;
  }
#undef PT_SIDES
#undef PT_SEEN
  free(hs);
  free(vs);
  free(vmap);
  free(ch);
  free(cp);
  if(err) cutgraph_free(g);
  return err;
}

// ---- Whole cut ----

static int pt_build(CutGraph * g, float width, float height, uint32_t count, uint8_t whimsy, uint8_t set,
    uint32_t seed, const PottsStyle * st) {
  Pt t;
  memset(&t, 0, sizeof(Pt));
  float res = st->res;
  t.w = (int)lroundf(width * res);
  t.h = (int)lroundf(height * res);
  t.count = count;
  rng_init(&t.r, seed);
  int n = t.w * t.h;
  t.lab = malloc(sizeof(int32_t) * n);
  t.frozen = calloc(n, 1);
  int err = !t.lab || !t.frozen;
  uint32_t nw = 0;
  if(!err) {
    for(int i = 0; i < n; i++) t.lab[i] = 0;
    nw = pt_whimsies(&t, whimsy_count(whimsy, count), set, res);
    t.nlab = count + nw;
    t.area = malloc(sizeof(int32_t) * t.nlab);
    t.perim = malloc(sizeof(int32_t) * t.nlab);
    err = !t.area || !t.perim;
  }
  if(!err) err = pt_blobs(&t, res);
  if(!err) {
    int free_squares = 0;
    for(int i = 0; i < n; i++) free_squares += !t.frozen[i];
    t.a0 = (float)free_squares / count;
    t.p0 = st->perim * 4 * sqrtf(t.a0);
    pt_unchecker(&t);
    pt_measure(&t);
    pt_run(&t, st);
    if(pt_unchecker(&t)) err = 1;
  }
  if(!err) err = pt_tograph(&t, g, res, st->smooth);
  free(t.lab);
  free(t.frozen);
  free(t.area);
  free(t.perim);
  return err;
}

int pottscut_graph(CutGraph * g, float width, float height, uint32_t count, uint8_t whimsy, uint8_t set,
    uint32_t seed, const PottsStyle * st) {
  memset(g, 0, sizeof(CutGraph));
  if(count < 2 || count > 0xFFF0 || !(width > 0) || !(height > 0) || st->res < 2) return 1;
  for(uint32_t t = 0; t < POTTSCUT_TRIES; t++) {
    if(!pt_build(g, width, height, count, whimsy, set, seed ^ (t * 0x85EBCA6Bu), st)) {
      g->tries = t + 1;
      return 0;
    }
  }
  return 1;
}

int pottscut_fromlabels(CutGraph * g, int32_t * lab, int w, int h, uint32_t count, float res, uint8_t smooth,
    const PottsStyle * run, uint32_t seed) {
  memset(g, 0, sizeof(CutGraph));
  Pt t;
  memset(&t, 0, sizeof(Pt));
  t.w = w;
  t.h = h;
  t.lab = lab;
  t.count = t.nlab = count;
  t.frozen = calloc((size_t)w * h, 1);
  int * queue = malloc(sizeof(int) * w * h);
  int32_t * comp = malloc(sizeof(int32_t) * w * h);
  int err = !t.frozen || !queue || !comp;
  if(!err) err = pt_keeplargest(&t, queue, comp);
  if(!err && run) {
    t.area = malloc(sizeof(int32_t) * count);
    t.perim = malloc(sizeof(int32_t) * count);
    err = !t.area || !t.perim;
    if(!err) {
      rng_init(&t.r, seed);
      t.a0 = (float)w * h / count;
      t.p0 = run->perim * 4 * sqrtf(t.a0);
      pt_unchecker(&t);
      pt_measure(&t);
      pt_run(&t, run);
    }
  }
  if(!err && pt_unchecker(&t)) err = 1;
  if(!err) err = pt_tograph(&t, g, res, smooth);
  free(t.area);
  free(t.perim);
  free(t.frozen);
  free(queue);
  free(comp);
  return err;
}

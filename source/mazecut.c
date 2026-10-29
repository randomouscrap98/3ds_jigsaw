#include "mazecut.h"
#include "pottscut.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

static const MazeStyle mazecut_styles[MAZECUT_STYLES] = {
  // Hexagonal, no warp (user: blur 0.2, straight 0.35 looked right; warps looked random)
  { .corridor = 0.45f, .straight = 0.35f, .res = 12, .blur = 0.2f, .smooth = 8, .warp = 0, .hex = 1 },
};

const MazeStyle * mazecut_style(uint8_t style) {
  return mazecut_styles + (style < MAZECUT_STYLES ? style : MAZECUT_STYLE_DEFAULT);
}

static int mz_build(CutGraph * g, float width, float height, uint32_t count, uint32_t seed, const MazeStyle * st) {
  Rng r;
  rng_init(&r, seed);
  // Hex: pointy-top hexagons of size hs (corridor = distance across flats), rows offset by
  // half a hexagon; stored as (column, row) with axial q = column - row / 2
  float hs = st->corridor / sqrtf(3);
  int mw = st->hex ? (int)ceilf(width / st->corridor) + 1 : (int)lroundf(width / st->corridor);
  int mh = st->hex ? (int)ceilf(height / (1.5f * hs)) + 1 : (int)lroundf(height / st->corridor);
  int mn = mw * mh, ndir = st->hex ? 6 : 4;
  if(mw < 2 || mh < 2 || (uint32_t)mn < count) return 1;
  // Dig: depth first from a random square, carrying on straight with some chance; the order
  // squares are first reached in is the order pieces are cut from
  int32_t * order = malloc(sizeof(int32_t) * mn), * stack = malloc(sizeof(int32_t) * mn);
  int8_t * cameby = malloc(mn);   // Direction the digging arrived in (-1 = start)
  int8_t * turned = calloc(mn, 1); // Turn taken to get here (directions, signed)
  uint8_t * seen = calloc(mn, 1);
  int32_t * mlab = malloc(sizeof(int32_t) * mn);
  int gw = (int)lroundf(width * st->res), gh = (int)lroundf(height * st->res), gn = gw * gh;
  int32_t * lab = malloc(sizeof(int32_t) * gn), * out = malloc(sizeof(int32_t) * gn);
  int err = !order || !stack || !cameby || !turned || !seen || !mlab || !lab || !out;
  const int dx[4] = { 1, 0, -1, 0 }, dy[4] = { 0, 1, 0, -1 };
  const int hq[6] = { 1, 1, 0, -1, -1, 0 }, hr[6] = { 0, -1, -1, 0, 1, 1 };
  if(!err) {
    int no = 0, sp = 0, s0 = rng_below(&r, mn);
    stack[sp++] = s0;
    seen[s0] = 1;
    cameby[s0] = -1;
    order[no++] = s0;
    while(sp) {
      int c = stack[sp - 1], cx = c % mw, cy = c / mw;
      int open[6], to[6], nopen = 0, straight = -1, sto = 0;
      for(int d = 0; d < ndir; d++) {
        int nx, ny = cy + (st->hex ? hr[d] : dy[d]);
        if(st->hex) nx = (cx - (cy >> 1) + hq[d]) + (ny >= 0 ? ny >> 1 : 0);
        else nx = cx + dx[d];
        if(nx < 0 || ny < 0 || nx >= mw || ny >= mh || seen[ny * mw + nx]) continue;
        to[nopen] = ny * mw + nx;
        open[nopen++] = d;
        if(d == cameby[c]) { straight = d; sto = ny * mw + nx; }
      }
      if(!nopen) { sp--; continue; }
      int pick = rng_below(&r, nopen);
      int d = open[pick], nc = to[pick];
      if(straight >= 0 && rng_range(&r, 0, 1) < st->straight) { d = straight; nc = sto; }
      else if(cameby[c] >= 0 && turned[c] && rng_range(&r, 0, 1) < st->curl) {
        // Turn the same way again (curl round)
        int sgn = rng_range(&r, 0, 1) < st->flip ? -turned[c] : turned[c];
        int want = ((cameby[c] + sgn) % ndir + ndir) % ndir;
        for(int k = 0; k < nopen; k++) if(open[k] == want) { d = want; nc = to[k]; }
      }
      seen[nc] = 1;
      int tv = cameby[c] < 0 ? 0 : ((d - cameby[c]) % ndir + ndir) % ndir;
      if(tv > ndir / 2) tv -= ndir;
      turned[nc] = (int8_t)(tv > 0 ? 1 : tv < 0 ? -1 : (cameby[c] >= 0 ? turned[c] : 0));
      cameby[nc] = d;
      order[no++] = nc;
      stack[sp++] = nc;
    }
    // Pieces: equal runs of the digging order (a run that jumps back after a dead end comes
    // apart; pottscut keeps each piece's largest part and hands the rest to neighbours)
    for(int i = 0; i < mn; i++) mlab[order[i]] = (int32_t)((int64_t)i * count / mn);
    // Displacement: a few long waves each way (wavelengths 2-5 cells)
    float wf[2][4][3];
    for(int a = 0; a < 2; a++) {
      for(int k = 0; k < 4; k++) {
        float wl = rng_range(&r, 2, 5), ang = rng_range(&r, 0, 2 * (float)M_PI);
        wf[a][k][0] = 2 * (float)M_PI / wl * cosf(ang);
        wf[a][k][1] = 2 * (float)M_PI / wl * sinf(ang);
        wf[a][k][2] = rng_range(&r, 0, 2 * (float)M_PI);
      }
    }
    // Fine grid, then the blurred vote: each square goes to the piece with the most weight
    // within the blur radius (weights fall off with distance)
    for(int y = 0; y < gh; y++) {
      for(int x = 0; x < gw; x++) {
        float px = (x + 0.5f) / st->res, py = (y + 0.5f) / st->res, ox = 0, oy = 0;
        for(int k = 0; k < 4; k++) {
          ox += sinf(wf[0][k][0] * px + wf[0][k][1] * py + wf[0][k][2]);
          oy += sinf(wf[1][k][0] * px + wf[1][k][1] * py + wf[1][k][2]);
        }
        px += ox * st->warp / 2;
        py += oy * st->warp / 2;
        if(px < 0) px = 0;
        if(py < 0) py = 0;
        int mx, my;
        if(st->hex) {
          // Which hexagon: axial coordinates, cube rounding
          float fq = (sqrtf(3) / 3 * px - py / 3) / hs, fr = 2.0f / 3 * py / hs, fs = -fq - fr;
          float rq = roundf(fq), rr = roundf(fr), rs = roundf(fs);
          float eq = fabsf(rq - fq), er = fabsf(rr - fr), es = fabsf(rs - fs);
          if(eq > er && eq > es) rq = -rr - rs;
          else if(er > es) rr = -rq - rs;
          my = (int)rr;
          mx = (int)rq + (my >= 0 ? my >> 1 : 0);
          if(my < 0) my = 0;
          if(mx < 0) mx = 0;
        } else {
          mx = (int)(px / width * mw);
          my = (int)(py / height * mh);
        }
        if(mx >= mw) mx = mw - 1;
        if(my >= mh) my = mh - 1;
        lab[y * gw + x] = mlab[my * mw + mx];
      }
    }
    int rad = (int)ceilf(st->blur * st->res);
    float sig2 = st->blur * st->res * st->blur * st->res / 2 + 1e-6f;
    int32_t cand[64];
    float wsum[64];
    for(int y = 0; y < gh; y++) {
      for(int x = 0; x < gw; x++) {
        int nc = 0;
        for(int v = -rad; v <= rad; v++) {
          for(int u = -rad; u <= rad; u++) {
            int qx = x + u, qy = y + v;
            if(qx < 0 || qy < 0 || qx >= gw || qy >= gh || u * u + v * v > rad * rad) continue;
            int32_t l = lab[qy * gw + qx];
            float wt = expf(-(u * u + v * v) / sig2);
            int k = 0;
            while(k < nc && cand[k] != l) k++;
            if(k == nc) {
              if(nc == 64) continue;
              cand[nc] = l;
              wsum[nc++] = 0;
            }
            wsum[k] += wt;
          }
        }
        int best = 0;
        for(int k = 1; k < nc; k++) if(wsum[k] > wsum[best]) best = k;
        out[y * gw + x] = cand[best];
      }
    }
    PottsStyle ps = *pottscut_style(POTTSCUT_STYLE_DEFAULT);
    ps.res = st->res;
    ps.sweeps = st->potts;
    ps.perim = st->pottsperim;
    ps.temp = st->pottstemp;
    err = pottscut_fromlabels(g, out, gw, gh, count, st->res, st->smooth, st->potts > 0 ? &ps : NULL, seed ^ 0x9077u);
  }
  free(order); free(stack); free(cameby); free(turned); free(seen); free(mlab); free(lab); free(out);
  return err;
}

int mazecut_graph(CutGraph * g, float width, float height, uint32_t count, uint32_t seed, const MazeStyle * st) {
  memset(g, 0, sizeof(CutGraph));
  if(count < 2 || count > 0xFFF0 || !(width > 0) || !(height > 0)) return 1;
  for(uint32_t t = 0; t < MAZECUT_TRIES; t++) {
    if(!mz_build(g, width, height, count, seed ^ (t * 0x85EBCA6Bu), st)) {
      g->tries = t + 1;
      return 0;
    }
  }
  return 1;
}

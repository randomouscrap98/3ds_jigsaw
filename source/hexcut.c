#include "hexcut.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

// Lattice units: x in thirds of the column spacing (column i's center at 3i), y in half
// hex heights (even columns' centers at 2j, odd columns' at 2j+1). A hex centered at
// (cx, cy) has corners (cx+-2, cy) and (cx+-1, cy+-1), and every border cut lands on
// lattice points, so clipping stays exact.

#define HEXCUT_RATIO 1.1547005f   // Regular hex: height / column spacing (2 / sqrt 3)
#define HEXCUT_MAXPTS 8           // A clipped hex's corners (at most 6, + slack)

uint32_t hexcut_count(uint16_t hcols, uint16_t hrows) {
  uint32_t even = (hcols + 1) / 2, odd = hcols / 2;
  return even * (hrows + 1u) + odd * hrows;
}

void hexcut_size(uint16_t cols, uint16_t rows, uint16_t * hcols, uint16_t * hrows) {
  float target = (float)cols * rows, best = INFINITY;
  *hcols = 2;
  *hrows = 1;
  for(uint32_t nr = 1; nr <= 2u * rows + 2; nr++) {
    for(uint32_t nc = 2; nc <= 2u * cols + 2; nc++) {
      float ratio = ((float)rows / nr) / ((float)cols / (nc - 1));
      float score = fabsf(hexcut_count(nc, nr) - target) / target + 2 * fabsf(logf(ratio / HEXCUT_RATIO));
      if(score < best) {
        best = score;
        *hcols = nc;
        *hrows = nr;
      }
    }
  }
}

// Clip polygon in (n points, x y pairs) to the half-plane dir * v >= dir * at, where v is
// coordinate axis (0 = x, 1 = y). Cuts land on lattice points here, so the division is exact.
// A corner sitting on the cut isn't repeated. Returns the new count.
static int hexcut_clip(const int * in, int n, int * out, int axis, int at, int dir) {
  int m = 0, o = 1 - axis;
  for(int i = 0; i < n; i++) {
    const int * p = in + 2 * i, * q = in + 2 * ((i + 1) % n);
    int pin = dir * p[axis] >= dir * at, qin = dir * q[axis] >= dir * at;
    if(pin) { out[2 * m] = p[0]; out[2 * m + 1] = p[1]; m++; }
    if(pin != qin && p[axis] != at && q[axis] != at) {
      out[2 * m + axis] = at;
      out[2 * m + o] = p[o] + (q[o] - p[o]) * (at - p[axis]) / (q[axis] - p[axis]);
      m++;
    }
  }
  return m;
}

int hexcut_graph(uint16_t cols, uint16_t rows, CutGraph * g) {
  memset(g, 0, sizeof(CutGraph));
  uint16_t nc, nr;
  hexcut_size(cols, rows, &nc, &nr);
  int xmax = 3 * (nc - 1), ymax = 2 * nr, lw = xmax + 1;
  uint32_t nl = (uint32_t)lw * (ymax + 1);
  g->count = hexcut_count(nc, nr);
  g->width = cols;
  g->height = rows;
  // Every lattice point gets a vertex slot (unused ones are dropped below); each vertex has
  // at most 3 edges, and each edge appears once per side
  int32_t * vid = malloc(sizeof(int32_t) * nl);
  uint32_t * vedge = malloc(sizeof(uint32_t) * 4 * nl);
  uint8_t * vdeg = calloc(nl, 1);
  uint32_t maxe = g->count * 6;
  g->e = calloc(maxe, sizeof(CutEdge));
  g->v = malloc(sizeof(CutVert) * nl);
  g->pts = malloc(sizeof(float) * 2);
  if(!vid || !vedge || !vdeg || !g->e || !g->v || !g->pts) goto fail;
  for(uint32_t i = 0; i < nl; i++) vid[i] = -1;
  float sx = (float)cols / xmax, sy = (float)rows / ymax;
  uint32_t p = 0;
  for(int i = 0; i < nc; i++) {
    int odd = i & 1, n = odd ? nr : nr + 1;
    for(int j = 0; j < n; j++, p++) {
      int cx = 3 * i, cy = 2 * j + odd;
      // Clockwise on screen (y down): right, bottom right, bottom left, left, top left, top right
      int hex[2 * HEXCUT_MAXPTS] = { cx + 2, cy, cx + 1, cy + 1, cx - 1, cy + 1, cx - 2, cy, cx - 1, cy - 1, cx + 1, cy - 1 };
      int tmp[2 * HEXCUT_MAXPTS], pts[2 * HEXCUT_MAXPTS];
      int m = hexcut_clip(hex, 6, tmp, 0, 0, 1);
      m = hexcut_clip(tmp, m, pts, 0, xmax, -1);
      m = hexcut_clip(pts, m, tmp, 1, 0, 1);
      m = hexcut_clip(tmp, m, pts, 1, ymax, -1);
      if(m < 3) goto fail;
      for(int k = 0; k < m; k++) {
        uint32_t l[2];
        for(int e = 0; e < 2; e++) {
          const int * q = pts + 2 * ((k + e) % m);
          l[e] = (uint32_t)q[1] * lw + q[0];
          if(vid[l[e]] < 0) {
            vid[l[e]] = (int32_t)g->nv;
            g->v[g->nv++] = (CutVert) { q[0] * sx, q[1] * sy };
          }
        }
        // This piece walks l[0] -> l[1]: left side if that's a -> b
        uint32_t a = vid[l[0]], b = vid[l[1]];
        int fwd = a < b;
        if(!fwd) { uint32_t s = a; a = b; b = s; }
        uint32_t la = fwd ? l[0] : l[1], lb = fwd ? l[1] : l[0];
        CutEdge * found = NULL;
        for(int d = 0; d < vdeg[la]; d++) {
          CutEdge * c = g->e + vedge[4 * la + d];
          if(c->b == b) found = c;
        }
        if(!found) {
          if(g->ne >= maxe || vdeg[la] >= 4 || vdeg[lb] >= 4) goto fail;
          found = g->e + g->ne;
          *found = (CutEdge) { .a = a, .b = b, .left = -1, .right = -1 };
          vedge[4 * la + vdeg[la]++] = g->ne;
          vedge[4 * lb + vdeg[lb]++] = g->ne;
          g->ne++;
        }
        if(fwd) found->left = (int32_t)p;
        else found->right = (int32_t)p;
      }
    }
  }
  free(vid);
  free(vedge);
  free(vdeg);
  if(cutgraph_rewalk(g)) {
    cutgraph_free(g);
    return 1;
  }
  return 0;
fail:
  free(vid);
  free(vedge);
  free(vdeg);
  cutgraph_free(g);
  return 1;
}

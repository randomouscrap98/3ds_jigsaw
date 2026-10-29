#include "cornerbend.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

int cornerbend_dirs(CutGraph * g) {
  // Edge ends by vertex: vstart[v]..vstart[v+1] in ends[] (edge * 2 + (1 if the end is b))
  uint32_t * vstart = calloc(g->nv + 1, sizeof(uint32_t));
  uint32_t * ends = malloc(sizeof(uint32_t) * 2 * (g->ne ? g->ne : 1));
  uint32_t * fill = malloc(sizeof(uint32_t) * (g->nv ? g->nv : 1));
  if(!vstart || !ends || !fill) {
    free(vstart); free(ends); free(fill);
    return 1;
  }
  for(uint32_t i = 0; i < g->ne; i++) {
    vstart[g->e[i].a + 1]++;
    vstart[g->e[i].b + 1]++;
  }
  for(uint32_t v = 0; v < g->nv; v++) vstart[v + 1] += vstart[v];
  memcpy(fill, vstart, sizeof(uint32_t) * g->nv);
  for(uint32_t i = 0; i < g->ne; i++) {
    ends[fill[g->e[i].a]++] = i * 2;
    ends[fill[g->e[i].b]++] = i * 2 + 1;
  }
  for(uint32_t v = 0; v < g->nv; v++) {
    const CutVert * p = g->v + v;
    uint32_t s = vstart[v], n = vstart[v + 1] - s;
    // Chord directions away from v
    float d[8][2];
    int curve[8];
    int any = 0, border = 0;
    if(n > 8) continue;
    for(uint32_t k = 0; k < n; k++) {
      const CutEdge * e = g->e + (ends[s + k] >> 1);
      const CutVert * q = g->v + ((ends[s + k] & 1) ? e->a : e->b);
      float dx = q->x - p->x, dy = q->y - p->y, l = sqrtf(dx * dx + dy * dy);
      d[k][0] = l > 0 ? dx / l : 0;
      d[k][1] = l > 0 ? dy / l : 0;
      curve[k] = e->left >= 0 && e->right >= 0 && !(e->flags & (CUTEDGE_WHIMSY | CUTEDGE_SNAKE)) && !e->npts;
      any |= curve[k];
      border |= e->left < 0 || e->right < 0;
    }
    if(!any) continue;
    float t[8][2];
    if(border) {
      // On the border: inner edges leave square on (normal to the side, pointing inward)
      float nx = p->x <= 1e-4f ? 1 : p->x >= g->width - 1e-4f ? -1 : 0;
      float ny = p->y <= 1e-4f ? 1 : p->y >= g->height - 1e-4f ? -1 : 0;
      for(uint32_t k = 0; k < n; k++) {
        t[k][0] = nx || ny ? nx : d[k][0];
        t[k][1] = nx || ny ? ny : d[k][1];
      }
    } else {
      // The straightest pair carries on through; everything else meets it square on
      int bi = 0, bj = 1;
      float best = 2;
      for(uint32_t i = 0; i < n; i++) {
        for(uint32_t j = i + 1; j < n; j++) {
          float dot = d[i][0] * d[j][0] + d[i][1] * d[j][1];   // -1 = straight through
          if(dot < best) { best = dot; bi = i; bj = j; }
        }
      }
      float lx = d[bj][0] - d[bi][0], ly = d[bj][1] - d[bi][1], ll = sqrtf(lx * lx + ly * ly);
      if(ll < 1e-6f) { lx = -d[bi][1]; ly = d[bi][0]; ll = 1; }
      lx /= ll; ly /= ll;
      for(uint32_t k = 0; k < n; k++) {
        if((int)k == bj) { t[k][0] = lx; t[k][1] = ly; }
        else if((int)k == bi) { t[k][0] = -lx; t[k][1] = -ly; }
        else {
          // Square on, toward its own side of the line
          float px = -ly, py = lx;
          if(px * d[k][0] + py * d[k][1] < 0) { px = -px; py = -py; }
          t[k][0] = px; t[k][1] = py;
        }
      }
    }
    for(uint32_t k = 0; k < n; k++) {
      if(!curve[k]) continue;
      CutEdge * e = g->e + (ends[s + k] >> 1);
      int atb = ends[s + k] & 1;
      e->dir[atb * 2] = t[k][0];
      e->dir[atb * 2 + 1] = t[k][1];
    }
  }
  // An edge needs both ends set (a corner with too many edges is skipped)
  for(uint32_t i = 0; i < g->ne; i++) {
    CutEdge * e = g->e + i;
    if((e->dir[0] == 0 && e->dir[1] == 0) || (e->dir[2] == 0 && e->dir[3] == 0)) {
      e->dir[0] = e->dir[1] = e->dir[2] = e->dir[3] = 0;
    }
  }
  free(vstart);
  free(ends);
  free(fill);
  return 0;
}

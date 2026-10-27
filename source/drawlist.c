#include "drawlist.h"

#include <stdlib.h>
#include <string.h>

int drawlist_init(DrawList * d, uint32_t count) {
  memset(d, 0, sizeof(DrawList));
  d->pieces = malloc(sizeof(uint16_t) * count);
  d->box = malloc(sizeof(float) * 4 * count);
  d->layer = malloc(sizeof(uint16_t) * count);
  d->key = malloc(sizeof(uint32_t) * count);
  if(!d->pieces || !d->box || !d->layer || !d->key) {
    drawlist_free(d);
    return 1;
  }
  d->count = count;
  for(uint32_t i = 0; i < count; i++) d->pieces[i] = i;
  return 0;
}

void drawlist_free(DrawList * d) {
  free(d->pieces);
  free(d->box);
  free(d->layer);
  free(d->key);
  memset(d, 0, sizeof(DrawList));
}

static int drawlist_overlap(const float * a, const float * b) {
  return a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3];
}

// Layer the groups order[from..to) among themselves, starting at base. Returns the
// highest layer used, or base - 1 if the range is empty. Compares every pair: the
// fallback when the grid can't get memory.
static int32_t drawlist_layers_all(DrawList * d, Board * b, uint32_t from, uint32_t to, int32_t base) {
  int32_t top = base - 1;
  for(uint32_t k = from; k < to; k++) {
    uint16_t gk = b->order[k];
    const float * bk = d->box + 4 * gk;
    int32_t l = base;
    for(uint32_t j = from; j < k; j++) {
      uint16_t gj = b->order[j];
      if(d->layer[gj] >= l && drawlist_overlap(bk, d->box + 4 * gj)) l = d->layer[gj] + 1;
    }
    d->layer[gk] = l;
    if(l > top) top = l;
  }
  return top;
}

// Coarse grid over the table: each square lists the groups whose box touches it, so a
// group is only compared with groups near it
typedef struct {
  float size;
  int32_t w, h;
  int32_t * head;       // Per square: first entry, -1 = empty
  struct { uint16_t group; int32_t next; } * ent;
  uint32_t nent, cap;
} DrawGrid;

// Square index for a coordinate, clamped to the grid (off-table boxes land on the edge squares)
static int32_t drawgrid_index(float v, float size, int32_t n) {
  if(v < 0) return 0;
  float i = v / size;
  return i >= n ? n - 1 : (int32_t)i;
}

static void drawgrid_range(DrawGrid * g, const float * bx, int32_t * c0, int32_t * r0, int32_t * c1, int32_t * r1) {
  *c0 = drawgrid_index(bx[0], g->size, g->w);
  *r0 = drawgrid_index(bx[1], g->size, g->h);
  *c1 = drawgrid_index(bx[2], g->size, g->w);
  *r1 = drawgrid_index(bx[3], g->size, g->h);
}

// Same result as drawlist_layers_all. Returns -2 if out of memory.
static int32_t drawlist_layers_grid(DrawList * d, Board * b, DrawGrid * g, uint32_t from, uint32_t to, int32_t base) {
  for(int32_t i = 0; i < g->w * g->h; i++) g->head[i] = -1;
  g->nent = 0;
  int32_t top = base - 1;
  for(uint32_t k = from; k < to; k++) {
    uint16_t gk = b->order[k];
    const float * bk = d->box + 4 * gk;
    int32_t c0, r0, c1, r1;
    drawgrid_range(g, bk, &c0, &r0, &c1, &r1);
    int32_t l = base;
    for(int32_t r = r0; r <= r1; r++) {
      for(int32_t c = c0; c <= c1; c++) {
        for(int32_t e = g->head[r * g->w + c]; e >= 0; e = g->ent[e].next) {
          uint16_t gj = g->ent[e].group;
          if(d->layer[gj] >= l && drawlist_overlap(bk, d->box + 4 * gj)) l = d->layer[gj] + 1;
        }
      }
    }
    d->layer[gk] = l;
    if(l > top) top = l;
    uint32_t need = g->nent + (uint32_t)((r1 - r0 + 1) * (c1 - c0 + 1));
    if(need > g->cap) {
      uint32_t cap = g->cap * 2 > need ? g->cap * 2 : need;
      void * n = realloc(g->ent, sizeof(*g->ent) * cap);
      if(!n) return -2;
      g->ent = n;
      g->cap = cap;
    }
    for(int32_t r = r0; r <= r1; r++) {
      for(int32_t c = c0; c <= c1; c++) {
        g->ent[g->nent].group = gk;
        g->ent[g->nent].next = g->head[r * g->w + c];
        g->head[r * g->w + c] = g->nent++;
      }
    }
  }
  return top;
}

// Layer groups order[from..to) from base, by grid if possible
static int32_t drawlist_layers(DrawList * d, Board * b, DrawGrid * g, uint32_t from, uint32_t to, int32_t base) {
  if(g->head) {
    int32_t top = drawlist_layers_grid(d, b, g, from, to, base);
    if(top != -2) return top;
  }
  return drawlist_layers_all(d, b, from, to, base);
}

void drawlist_build(DrawList * d, Board * b, float pad, const uint8_t * page, uint32_t pages, uint32_t ntop) {
  if(ntop > b->ngroups) ntop = b->ngroups;
  // Group bounding boxes from piece centers
  for(uint32_t i = 0; i < b->ngroups; i++) {
    float * bx = d->box + 4 * b->order[i];
    bx[0] = bx[1] = 1e30f;
    bx[2] = bx[3] = -1e30f;
  }
  for(uint32_t p = 0; p < b->count; p++) {
    float * bx = d->box + 4 * b->pieces[p].group;
    float x, y;
    board_piece_center(b, p, &x, &y);
    if(x - pad < bx[0]) bx[0] = x - pad;
    if(y - pad < bx[1]) bx[1] = y - pad;
    if(x + pad > bx[2]) bx[2] = x + pad;
    if(y + pad > bx[3]) bx[3] = y + pad;
  }
  // Squares about two sprites wide; capped so a tiny pad can't make a huge grid
  DrawGrid g = { .size = pad * 4 };
  if(g.size < b->width / DRAWLIST_MAXGRID) g.size = b->width / DRAWLIST_MAXGRID;
  if(g.size < b->height / DRAWLIST_MAXGRID) g.size = b->height / DRAWLIST_MAXGRID;
  if(g.size <= 0) g.size = 1;
  g.w = (int32_t)(b->width / g.size) + 1;
  g.h = (int32_t)(b->height / g.size) + 1;
  g.cap = b->ngroups * 4;
  g.head = malloc(sizeof(int32_t) * g.w * g.h);
  g.ent = malloc(sizeof(*g.ent) * g.cap);
  if(!g.head || !g.ent) {
    free(g.head);
    g.head = NULL;
  }
  if(d->nogrid) {
    free(g.head);
    g.head = NULL;
  }
  uint32_t split = b->ngroups - ntop;
  int32_t top = drawlist_layers(d, b, &g, 0, split, 0);
  top = drawlist_layers(d, b, &g, split, b->ngroups, top + 1);
  free(g.head);
  free(g.ent);
  d->layers = top + 1;
  // Counting sort of pieces by (layer, page)
  if(pages == 0) pages = 1;
  uint32_t nkeys = d->layers * pages;
  uint32_t * start = calloc(nkeys + 1, sizeof(uint32_t));
  if(!start) {
    // No memory for the sort: plain board order is always correct, just slower
    uint32_t n = 0;
    for(uint32_t i = 0; i < b->ngroups; i++) {
      for(uint16_t p = b->groups[b->order[i]].first; p != BOARD_NONE; p = b->pieces[p].next) d->pieces[n++] = p;
    }
    return;
  }
  for(uint32_t p = 0; p < b->count; p++) {
    d->key[p] = d->layer[b->pieces[p].group] * pages + (page[p] < pages ? page[p] : pages - 1);
    start[d->key[p] + 1]++;
  }
  for(uint32_t k = 0; k < nkeys; k++) start[k + 1] += start[k];
  for(uint32_t p = 0; p < b->count; p++) d->pieces[start[d->key[p]]++] = p;
  free(start);
}

#include "board.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

void board_rotvec(uint8_t rot, float x, float y, float * ox, float * oy) {
  switch(rot & 3) {
    case 0: *ox = x;  *oy = y;  break;
    case 1: *ox = -y; *oy = x;  break;
    case 2: *ox = -x; *oy = -y; break;
    default: *ox = y; *oy = -x; break;
  }
}

int board_init(Board * b, uint16_t cols, uint16_t rows, float cell, float width, float height) {
  memset(b, 0, sizeof(Board));
  uint32_t count = (uint32_t)cols * rows;
  if(count == 0 || count > BOARD_MAXPIECES) return 1;
  b->pieces = malloc(sizeof(BoardPiece) * count);
  b->groups = malloc(sizeof(BoardGroup) * count);
  b->order = malloc(sizeof(uint16_t) * count);
  if(!b->pieces || !b->groups || !b->order) {
    board_free(b);
    return 1;
  }
  b->count = count;
  b->ngroups = count;
  b->cols = cols;
  b->rows = rows;
  b->cell = cell;
  b->width = width;
  b->height = height;
  for(uint32_t i = 0; i < count; i++) {
    b->pieces[i].group = i;
    b->pieces[i].next = BOARD_NONE;
    b->groups[i] = (BoardGroup) { .x = 0, .y = 0, .first = i, .count = 1, .rot = 0 };
    b->order[i] = i;
  }
  return 0;
}

void board_free(Board * b) {
  free(b->pieces);
  free(b->groups);
  free(b->order);
  b->pieces = NULL;
  b->groups = NULL;
  b->order = NULL;
  b->count = 0;
  b->ngroups = 0;
}

// Position a single-piece group so that piece's center lands on (cx, cy)
static void board_place_single(Board * b, uint16_t piece, float cx, float cy, uint8_t rot) {
  BoardGroup * g = b->groups + b->pieces[piece].group;
  float lx = ((piece % b->cols) + 0.5f) * b->cell;
  float ly = ((piece / b->cols) + 0.5f) * b->cell;
  float rx, ry;
  board_rotvec(rot, lx, ly, &rx, &ry);
  g->rot = rot & 3;
  g->x = cx - rx;
  g->y = cy - ry;
}

void board_scatter(Board * b, uint32_t seed) {
  Rng rng;
  rng_init(&rng, seed);
  // Reset to one group per piece
  for(uint32_t i = 0; i < b->count; i++) {
    b->pieces[i].group = i;
    b->pieces[i].next = BOARD_NONE;
    b->groups[i] = (BoardGroup) { .x = 0, .y = 0, .first = i, .count = 1, .rot = 0 };
  }
  b->ngroups = b->count;
  // Lay a grid of slots over the table with at least one slot per piece
  float slot = sqrtf(b->width * b->height / b->count);
  uint32_t scols, srows;
  for(;;) {
    scols = (uint32_t)(b->width / slot);
    srows = (uint32_t)(b->height / slot);
    if(scols * srows >= b->count) break;
    slot *= 0.95f;
  }
  uint32_t nslots = scols * srows;
  uint32_t * slots = malloc(sizeof(uint32_t) * nslots);
  if(!slots) {
    // Degenerate fallback: stack everything in the middle
    for(uint32_t i = 0; i < b->count; i++) {
      board_place_single(b, i, b->width / 2, b->height / 2, rng_below(&rng, 4));
    }
  } else {
    for(uint32_t i = 0; i < nslots; i++) slots[i] = i;
    for(uint32_t i = nslots - 1; i > 0; i--) {
      uint32_t j = rng_below(&rng, i + 1);
      uint32_t t = slots[i]; slots[i] = slots[j]; slots[j] = t;
    }
    // Pieces with tabs are ~1.7 cells across; jitter within whatever slack is left
    float slack = (slot - b->cell * 1.7f) / 2;
    if(slack < 0) slack = 0;
    float offx = (b->width - scols * slot) / 2;
    float offy = (b->height - srows * slot) / 2;
    for(uint32_t i = 0; i < b->count; i++) {
      uint32_t s = slots[i];
      float cx = offx + ((s % scols) + 0.5f) * slot + rng_range(&rng, -slack, slack);
      float cy = offy + ((s / scols) + 0.5f) * slot + rng_range(&rng, -slack, slack);
      board_place_single(b, i, cx, cy, rng_below(&rng, 4));
    }
    free(slots);
  }
  // Random draw order too, so it doesn't look like a scan pattern when things overlap
  for(uint32_t i = 0; i < b->count; i++) b->order[i] = i;
  for(uint32_t i = b->count - 1; i > 0; i--) {
    uint32_t j = rng_below(&rng, i + 1);
    uint16_t t = b->order[i]; b->order[i] = b->order[j]; b->order[j] = t;
  }
}

int32_t board_pick(Board * b, float x, float y) {
  for(int32_t i = (int32_t)b->ngroups - 1; i >= 0; i--) {
    uint16_t gid = b->order[i];
    BoardGroup * g = b->groups + gid;
    // Undo the group transform to land in grid space
    float lx, ly;
    board_rotvec((4 - g->rot) & 3, x - g->x, y - g->y, &lx, &ly);
    if(lx < 0 || ly < 0) continue;
    uint32_t col = (uint32_t)(lx / b->cell);
    uint32_t row = (uint32_t)(ly / b->cell);
    if(col >= b->cols || row >= b->rows) continue;
    if(b->pieces[row * b->cols + col].group == gid) return gid;
  }
  return -1;
}

void board_raise(Board * b, uint16_t group) {
  uint32_t i;
  for(i = 0; i < b->ngroups; i++) {
    if(b->order[i] == group) break;
  }
  if(i >= b->ngroups) return;
  memmove(b->order + i, b->order + i + 1, sizeof(uint16_t) * (b->ngroups - i - 1));
  b->order[b->ngroups - 1] = group;
}

void board_move(Board * b, uint16_t group, float dx, float dy) {
  b->groups[group].x += dx;
  b->groups[group].y += dy;
}

void board_rotate(Board * b, uint16_t group, int dir, float px, float py) {
  BoardGroup * g = b->groups + group;
  uint8_t r = dir > 0 ? 1 : 3;
  float rx, ry;
  board_rotvec(r, g->x - px, g->y - py, &rx, &ry);
  g->x = px + rx;
  g->y = py + ry;
  g->rot = (g->rot + r) & 3;
}

// Merge group src into dst (dst survives). Assumes they're already aligned.
static void board_merge(Board * b, uint16_t dst, uint16_t src) {
  BoardGroup * d = b->groups + dst;
  BoardGroup * s = b->groups + src;
  uint16_t p = s->first, last = BOARD_NONE;
  while(p != BOARD_NONE) {
    b->pieces[p].group = dst;
    last = p;
    p = b->pieces[p].next;
  }
  if(last != BOARD_NONE) {
    b->pieces[last].next = d->first;
    d->first = s->first;
  }
  d->count += s->count;
  s->count = 0;
  s->first = BOARD_NONE;
  // Remove src from draw order
  uint32_t i;
  for(i = 0; i < b->ngroups; i++) {
    if(b->order[i] == src) break;
  }
  if(i < b->ngroups) {
    memmove(b->order + i, b->order + i + 1, sizeof(uint16_t) * (b->ngroups - i - 1));
    b->ngroups--;
  }
}

// Find any group that can merge with gid. Returns group id or -1
static int32_t board_find_merge(Board * b, uint16_t gid, float snap) {
  BoardGroup * g = b->groups + gid;
  float snap2 = snap * snap;
  for(uint16_t p = g->first; p != BOARD_NONE; p = b->pieces[p].next) {
    uint32_t col = p % b->cols, row = p / b->cols;
    int32_t nb[4] = {
      row > 0 ? (int32_t)(p - b->cols) : -1,
      row + 1 < b->rows ? (int32_t)(p + b->cols) : -1,
      col > 0 ? (int32_t)(p - 1) : -1,
      col + 1 < b->cols ? (int32_t)(p + 1) : -1,
    };
    for(int i = 0; i < 4; i++) {
      if(nb[i] < 0) continue;
      uint16_t hid = b->pieces[nb[i]].group;
      if(hid == gid) continue;
      BoardGroup * h = b->groups + hid;
      if(h->rot != g->rot) continue;
      float dx = h->x - g->x, dy = h->y - g->y;
      if(dx * dx + dy * dy <= snap2) return hid;
    }
  }
  return -1;
}

int board_drop(Board * b, uint16_t group, float snap) {
  int merges = 0;
  for(;;) {
    int32_t other = board_find_merge(b, group, snap);
    if(other < 0) break;
    b->groups[group].x = b->groups[other].x;
    b->groups[group].y = b->groups[other].y;
    board_merge(b, group, other);
    merges++;
  }
  return merges;
}

int board_complete(Board * b) {
  return b->ngroups == 1;
}

void board_piece_center(Board * b, uint16_t piece, float * x, float * y) {
  BoardGroup * g = b->groups + b->pieces[piece].group;
  float rx, ry;
  board_rotvec(g->rot, ((piece % b->cols) + 0.5f) * b->cell,
      ((piece / b->cols) + 0.5f) * b->cell, &rx, &ry);
  *x = g->x + rx;
  *y = g->y + ry;
}

uint8_t board_piece_rot(Board * b, uint16_t piece) {
  return b->groups[b->pieces[piece].group].rot;
}

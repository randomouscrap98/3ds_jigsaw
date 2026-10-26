#include "boardsave.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BOARDSAVE_HEADER 18
#define BOARDSAVE_GROUP 9

static void put16(uint8_t ** p, uint16_t v) {
  (*p)[0] = v & 0xFF; (*p)[1] = v >> 8;
  *p += 2;
}
static void put32(uint8_t ** p, uint32_t v) {
  for(int i = 0; i < 4; i++) (*p)[i] = (v >> (8 * i)) & 0xFF;
  *p += 4;
}
static void putf(uint8_t ** p, float f) {
  uint32_t v;
  memcpy(&v, &f, 4);
  put32(p, v);
}
static uint16_t get16(const uint8_t ** p) {
  uint16_t v = (*p)[0] | ((*p)[1] << 8);
  *p += 2;
  return v;
}
static uint32_t get32(const uint8_t ** p) {
  uint32_t v = 0;
  for(int i = 0; i < 4; i++) v |= (uint32_t)(*p)[i] << (8 * i);
  *p += 4;
  return v;
}
static float getf(const uint8_t ** p) {
  uint32_t v = get32(p);
  float f;
  memcpy(&f, &v, 4);
  return f;
}

size_t boardsave_size(Board * b) {
  return BOARDSAVE_HEADER + BOARDSAVE_GROUP * b->ngroups + 2 * b->count;
}

size_t boardsave_write(Board * b, uint8_t * buf) {
  uint8_t * p = buf;
  memcpy(p, "JIGS", 4);
  p += 4;
  put16(&p, BOARDSAVE_VERSION);
  put16(&p, b->cols);
  put16(&p, b->rows);
  putf(&p, b->cell);
  put32(&p, b->ngroups);
  for(uint32_t i = 0; i < b->ngroups; i++) {
    BoardGroup * g = b->groups + b->order[i];
    putf(&p, g->x);
    putf(&p, g->y);
    *p++ = g->rot;
  }
  // Map group id -> index in order. Reuse the piece "next" walk to avoid an allocation.
  for(uint32_t i = 0; i < b->ngroups; i++) {
    for(uint16_t pc = b->groups[b->order[i]].first; pc != BOARD_NONE; pc = b->pieces[pc].next) {
      uint8_t * q = buf + BOARDSAVE_HEADER + BOARDSAVE_GROUP * b->ngroups + 2 * pc;
      put16(&q, i);
    }
  }
  return boardsave_size(b);
}

int boardsave_peek(const uint8_t * buf, size_t len, BoardSaveInfo * info) {
  if(len < BOARDSAVE_HEADER || memcmp(buf, "JIGS", 4)) return 1;
  const uint8_t * p = buf + 4;
  info->version = get16(&p);
  info->cols = get16(&p);
  info->rows = get16(&p);
  info->cell = getf(&p);
  info->ngroups = get32(&p);
  if(info->version != BOARDSAVE_VERSION) return 2;
  return 0;
}

int boardsave_read(Board * b, const uint8_t * buf, size_t len) {
  BoardSaveInfo info;
  int err = boardsave_peek(buf, len, &info);
  if(err) return err;
  if(info.cols != b->cols || info.rows != b->rows) return 3;
  if(info.ngroups == 0 || info.ngroups > b->count) return 4;
  if(len < BOARDSAVE_HEADER + BOARDSAVE_GROUP * info.ngroups + 2 * b->count) return 5;
  if(!(info.cell > 0) || !isfinite(info.cell)) return 6;
  float scale = b->cell / info.cell;
  // Validate everything before touching the board
  const uint8_t * gp = buf + BOARDSAVE_HEADER;
  const uint8_t * pp = gp + BOARDSAVE_GROUP * info.ngroups;
  uint32_t * counts = calloc(info.ngroups, sizeof(uint32_t));
  if(!counts) return 7;
  err = 0;
  for(uint32_t i = 0; i < b->count && !err; i++) {
    const uint8_t * q = pp + 2 * i;
    uint16_t gi = get16(&q);
    if(gi >= info.ngroups) err = 8;
    else counts[gi]++;
  }
  for(uint32_t i = 0; i < info.ngroups && !err; i++) {
    if(counts[i] == 0) err = 9;
    const uint8_t * q = gp + BOARDSAVE_GROUP * i;
    float x = getf(&q), y = getf(&q);
    if(!isfinite(x) || !isfinite(y) || q[0] > 3) err = 10;
  }
  free(counts);
  if(err) return err;
  // Apply. Group ids are simply the saved indices.
  for(uint32_t i = 0; i < b->count; i++) {
    b->groups[i] = (BoardGroup) { .x = 0, .y = 0, .first = BOARD_NONE, .count = 0, .rot = 0 };
  }
  for(uint32_t i = 0; i < info.ngroups; i++) {
    const uint8_t * q = gp + BOARDSAVE_GROUP * i;
    b->groups[i].x = getf(&q) * scale;
    b->groups[i].y = getf(&q) * scale;
    b->groups[i].rot = q[0];
    b->order[i] = i;
  }
  b->ngroups = info.ngroups;
  for(uint32_t i = 0; i < b->count; i++) {
    const uint8_t * q = pp + 2 * i;
    uint16_t gi = get16(&q);
    BoardGroup * g = b->groups + gi;
    b->pieces[i].group = gi;
    b->pieces[i].next = g->first;
    g->first = i;
    g->count++;
  }
  return 0;
}

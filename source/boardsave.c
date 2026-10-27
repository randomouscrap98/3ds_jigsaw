#include "boardsave.h"

#include "bytes.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BOARDSAVE_HEADER 18      // Version 1
#define BOARDSAVE_HEADER2 22     // Version 2 (+ count)
#define BOARDSAVE_GROUP 9

// Version 1 unless the board has pieces beyond the grid
static int boardsave_v2(Board * b) {
  return b->count != (uint32_t)b->geo->cols * b->geo->rows;
}

static size_t boardsave_header(uint16_t version) {
  return version >= 2 ? BOARDSAVE_HEADER2 : BOARDSAVE_HEADER;
}

size_t boardsave_size(Board * b) {
  return boardsave_header(boardsave_v2(b) ? 2 : 1) + BOARDSAVE_GROUP * b->ngroups + 2 * b->count;
}

size_t boardsave_write(Board * b, uint8_t * buf) {
  uint8_t * p = buf;
  memcpy(p, "JIGS", 4);
  p += 4;
  int v2 = boardsave_v2(b);
  put16(&p, v2 ? 2 : 1);
  put16(&p, b->geo->cols);
  put16(&p, b->geo->rows);
  putf(&p, b->geo->cell);
  put32(&p, b->ngroups);
  if(v2) put32(&p, b->count);
  size_t head = boardsave_header(v2 ? 2 : 1);
  for(uint32_t i = 0; i < b->ngroups; i++) {
    BoardGroup * g = b->groups + b->order[i];
    putf(&p, g->x);
    putf(&p, g->y);
    *p++ = g->rot;
  }
  // Map group id -> index in order. Reuse the piece "next" walk to avoid an allocation.
  for(uint32_t i = 0; i < b->ngroups; i++) {
    for(uint16_t pc = b->groups[b->order[i]].first; pc != BOARD_NONE; pc = b->pieces[pc].next) {
      uint8_t * q = buf + head + BOARDSAVE_GROUP * b->ngroups + 2 * pc;
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
  if(info->version < 1 || info->version > BOARDSAVE_VERSION) return 2;
  if(info->version >= 2) {
    if(len < BOARDSAVE_HEADER2) return 1;
    info->count = get32(&p);
  } else {
    info->count = (uint32_t)info->cols * info->rows;
  }
  return 0;
}

int boardsave_read(Board * b, const uint8_t * buf, size_t len) {
  BoardSaveInfo info;
  int err = boardsave_peek(buf, len, &info);
  if(err) return err;
  if(info.cols != b->geo->cols || info.rows != b->geo->rows || info.count != b->count) return 3;
  if(info.ngroups == 0 || info.ngroups > b->count) return 4;
  size_t head = boardsave_header(info.version);
  if(len < head + BOARDSAVE_GROUP * info.ngroups + 2 * b->count) return 5;
  if(!(info.cell > 0) || !isfinite(info.cell)) return 6;
  float scale = b->geo->cell / info.cell;
  // Validate everything before touching the board
  const uint8_t * gp = buf + head;
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

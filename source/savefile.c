#include "savefile.h"
#include "boardsave.h"

#include <string.h>

void savefile_init(SaveHeader * h) {
  memset(h, 0, sizeof(SaveHeader));
  h->version = SAVEFILE_VERSION;
  h->size = SAVEFILE_HEADER;
}

size_t savefile_size(Board * b) {
  return SAVEFILE_HEADER + boardsave_size(b);
}

void savefile_write(const SaveHeader * h, Board * b, uint8_t * buf) {
  memset(buf, 0, SAVEFILE_HEADER);
  memcpy(buf, SAVEFILE_MAGIC, 8);
  buf[8] = h->version & 0xFF; buf[9] = h->version >> 8;
  buf[10] = SAVEFILE_HEADER & 0xFF; buf[11] = SAVEFILE_HEADER >> 8;
  memcpy(buf + 12, h->appver, 3);
  buf[15] = h->style;
  buf[16] = h->target & 0xFF; buf[17] = h->target >> 8;
  buf[18] = h->table;
  boardsave_write(b, buf + SAVEFILE_HEADER);
}

int savefile_readheader(const uint8_t * buf, size_t len, SaveHeader * h) {
  if(len < SAVEFILE_MINHEADER || memcmp(buf, SAVEFILE_MAGIC, 8)) return SAVEFILE_BAD;
  memset(h, 0, sizeof(SaveHeader));
  h->version = buf[8] | (buf[9] << 8);
  h->size = buf[10] | (buf[11] << 8);
  if(h->version > SAVEFILE_VERSION) return SAVEFILE_NEWER;
  if(h->size < SAVEFILE_MINHEADER || h->size > len) return SAVEFILE_BAD;
  memcpy(h->appver, buf + 12, 3);
  h->style = buf[15];
  h->target = buf[16] | (buf[17] << 8);
  h->table = buf[18];
  return SAVEFILE_OK;
}

int savefile_peek(const uint8_t * buf, size_t len, SaveInfo * info) {
  int r = savefile_readheader(buf, len, &info->header);
  if(r) return r;
  BoardSaveInfo bi;
  if(boardsave_peek(buf + info->header.size, len - info->header.size, &bi)) return SAVEFILE_BAD;
  info->pieces = (uint32_t)bi.cols * bi.rows;
  info->groups = bi.ngroups;
  return SAVEFILE_OK;
}

int savefile_readboard(const uint8_t * buf, size_t len, const SaveHeader * h, Board * b) {
  if(h->size > len) return SAVEFILE_BAD;
  return boardsave_read(b, buf + h->size, len - h->size) ? SAVEFILE_BAD : SAVEFILE_OK;
}

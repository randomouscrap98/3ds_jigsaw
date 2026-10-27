#include "savefile.h"
#include "boardsave.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>

#define SAVEFILE_SEEDAT 30

uint32_t savefile_cutseed(const char * name, uint32_t target, uint8_t style) {
  return rng_hashstr(name) ^ (target * 2654435761u) ^ (style * 0x9E3779B9u);
}

uint32_t savefile_seedfor(const SaveHeader * h, const char * name) {
  return h->seed ? h->seed : savefile_cutseed(name, h->target, h->style);
}

uint8_t * savefile_withseed(const uint8_t * buf, size_t len, const SaveHeader * h, uint32_t seed, size_t * outlen) {
  size_t hs = h->size < SAVEFILE_SEEDAT + 4 ? SAVEFILE_HEADER : h->size;
  *outlen = len - h->size + hs;
  uint8_t * out = calloc(*outlen, 1);
  if(!out) return NULL;
  memcpy(out, buf, h->size);
  memcpy(out + hs, buf + h->size, len - h->size);
  out[10] = hs & 0xFF; out[11] = hs >> 8;
  for(int i = 0; i < 4; i++) out[SAVEFILE_SEEDAT + i] = (seed >> (8 * i)) & 0xFF;
  return out;
}

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
  for(int i = 0; i < 4; i++) buf[19 + i] = (h->seconds >> (8 * i)) & 0xFF;
  buf[23] = h->music;
  buf[24] = h->whimsy;
  buf[25] = h->whimsyset;
  for(int i = 0; i < 4; i++) buf[26 + i] = (h->cuthash >> (8 * i)) & 0xFF;
  for(int i = 0; i < 4; i++) buf[SAVEFILE_SEEDAT + i] = (h->seed >> (8 * i)) & 0xFF;
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
  for(int i = 0; i < 4; i++) h->seconds |= (uint32_t)buf[19 + i] << (8 * i);
  if(h->size > 23) h->music = buf[23];
  if(h->music > SAVEFILE_MUSIC_SHUFFLE) h->music = SAVEFILE_MUSIC_ORDER;
  if(h->size > 25) {
    h->whimsy = buf[24];
    h->whimsyset = buf[25];
  }
  if(h->size > 29) {
    for(int i = 0; i < 4; i++) h->cuthash |= (uint32_t)buf[26 + i] << (8 * i);
  }
  if(h->size >= SAVEFILE_SEEDAT + 4) {
    for(int i = 0; i < 4; i++) h->seed |= (uint32_t)buf[SAVEFILE_SEEDAT + i] << (8 * i);
  }
  return SAVEFILE_OK;
}

int savefile_peek(const uint8_t * buf, size_t len, SaveInfo * info) {
  int r = savefile_readheader(buf, len, &info->header);
  if(r) return r;
  BoardSaveInfo bi;
  if(boardsave_peek(buf + info->header.size, len - info->header.size, &bi)) return SAVEFILE_BAD;
  info->pieces = bi.count;
  info->groups = bi.ngroups;
  return SAVEFILE_OK;
}

int savefile_readboard(const uint8_t * buf, size_t len, const SaveHeader * h, Board * b) {
  if(h->size > len) return SAVEFILE_BAD;
  return boardsave_read(b, buf + h->size, len - h->size) ? SAVEFILE_BAD : SAVEFILE_OK;
}

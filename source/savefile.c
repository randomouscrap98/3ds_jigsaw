#include "savefile.h"
#include "boardsave.h"
#include "rng.h"

#include <stdlib.h>
#include <string.h>

_Static_assert(SAVEFILE_AT_END <= SAVEFILE_HEADER, "header fields fit in SAVEFILE_HEADER");

static void sf_put16(uint8_t * buf, int at, uint16_t v) { buf[at] = v & 0xFF; buf[at + 1] = v >> 8; }
static void sf_put32(uint8_t * buf, int at, uint32_t v) { for(int i = 0; i < 4; i++) buf[at + i] = (v >> (8 * i)) & 0xFF; }
static uint16_t sf_get16(const uint8_t * buf, int at) { return buf[at] | (buf[at + 1] << 8); }
static uint32_t sf_get32(const uint8_t * buf, int at) {
  uint32_t v = 0;
  for(int i = 0; i < 4; i++) v |= (uint32_t)buf[at + i] << (8 * i);
  return v;
}
// Field of n bytes at at is in a header of size bytes (older versions wrote shorter ones)
#define SF_HAS(size, at, n) ((size) >= (at) + (n))

uint32_t savefile_cutseed(const char * name, uint32_t target, uint8_t style) {
  return rng_hashstr(name) ^ (target * 2654435761u) ^ (style * 0x9E3779B9u);
}

uint32_t savefile_seedfor(const SaveHeader * h, const char * name) {
  return h->seed ? h->seed : savefile_cutseed(name, h->target, h->style);
}

uint8_t * savefile_withseed(const uint8_t * buf, size_t len, const SaveHeader * h, uint32_t seed, size_t * outlen) {
  size_t hs = SF_HAS(h->size, SAVEFILE_AT_SEED, 4) ? h->size : SAVEFILE_HEADER;
  *outlen = len - h->size + hs;
  uint8_t * out = calloc(*outlen, 1);
  if(!out) return NULL;
  memcpy(out, buf, h->size);
  memcpy(out + hs, buf + h->size, len - h->size);
  sf_put16(out, SAVEFILE_AT_SIZE, hs);
  sf_put32(out, SAVEFILE_AT_SEED, seed);
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
  memcpy(buf + SAVEFILE_AT_MAGIC, SAVEFILE_MAGIC, 8);
  sf_put16(buf, SAVEFILE_AT_VERSION, h->version);
  sf_put16(buf, SAVEFILE_AT_SIZE, SAVEFILE_HEADER);
  memcpy(buf + SAVEFILE_AT_APPVER, h->appver, 3);
  buf[SAVEFILE_AT_STYLE] = h->style;
  sf_put16(buf, SAVEFILE_AT_TARGET, h->target);
  buf[SAVEFILE_AT_TABLE] = h->table;
  sf_put32(buf, SAVEFILE_AT_SECONDS, h->seconds);
  buf[SAVEFILE_AT_MUSIC] = h->music;
  buf[SAVEFILE_AT_WHIMSY] = h->whimsy;
  buf[SAVEFILE_AT_WHIMSYSET] = h->whimsyset;
  sf_put32(buf, SAVEFILE_AT_CUTHASH, h->cuthash);
  sf_put32(buf, SAVEFILE_AT_SEED, h->seed);
  buf[SAVEFILE_AT_OVERLAY] = h->overlay;
  buf[SAVEFILE_AT_VOLUME] = h->volume;
  buf[SAVEFILE_AT_TOP] = h->top;
  boardsave_write(b, buf + SAVEFILE_HEADER);
}

int savefile_readheader(const uint8_t * buf, size_t len, SaveHeader * h) {
  if(len < SAVEFILE_MINHEADER || memcmp(buf + SAVEFILE_AT_MAGIC, SAVEFILE_MAGIC, 8)) return SAVEFILE_BAD;
  memset(h, 0, sizeof(SaveHeader));
  h->version = sf_get16(buf, SAVEFILE_AT_VERSION);
  h->size = sf_get16(buf, SAVEFILE_AT_SIZE);
  if(h->version > SAVEFILE_VERSION) return SAVEFILE_NEWER;
  if(h->size < SAVEFILE_MINHEADER || h->size > len) return SAVEFILE_BAD;
  memcpy(h->appver, buf + SAVEFILE_AT_APPVER, 3);
  h->style = buf[SAVEFILE_AT_STYLE];
  h->target = sf_get16(buf, SAVEFILE_AT_TARGET);
  h->table = buf[SAVEFILE_AT_TABLE];
  h->seconds = sf_get32(buf, SAVEFILE_AT_SECONDS);
  // Later fields: only if this header has them (0 otherwise, from the memset)
  int hs = h->size;
  if(SF_HAS(hs, SAVEFILE_AT_MUSIC, 1)) h->music = buf[SAVEFILE_AT_MUSIC];
  if(SF_HAS(hs, SAVEFILE_AT_WHIMSY, 1)) h->whimsy = buf[SAVEFILE_AT_WHIMSY];
  if(SF_HAS(hs, SAVEFILE_AT_WHIMSYSET, 1)) h->whimsyset = buf[SAVEFILE_AT_WHIMSYSET];
  if(SF_HAS(hs, SAVEFILE_AT_CUTHASH, 4)) h->cuthash = sf_get32(buf, SAVEFILE_AT_CUTHASH);
  if(SF_HAS(hs, SAVEFILE_AT_SEED, 4)) h->seed = sf_get32(buf, SAVEFILE_AT_SEED);
  if(SF_HAS(hs, SAVEFILE_AT_OVERLAY, 1)) h->overlay = buf[SAVEFILE_AT_OVERLAY];
  if(SF_HAS(hs, SAVEFILE_AT_VOLUME, 1)) h->volume = buf[SAVEFILE_AT_VOLUME];
  if(SF_HAS(hs, SAVEFILE_AT_TOP, 1)) h->top = buf[SAVEFILE_AT_TOP];
  if(h->music >= SAVEFILE_MUSIC_MODES) h->music = SAVEFILE_MUSIC_ORDER;
  if(h->volume >= SAVEFILE_VOLUMES) h->volume = SAVEFILE_VOLUME_HIGH;
  if(h->top >= SAVEFILE_TOPS) h->top = SAVEFILE_TOP_PICTURE;
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

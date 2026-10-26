#include "savefile.h"

#include <string.h>

void savefile_init(SaveHeader * h) {
  memset(h, 0, sizeof(SaveHeader));
  h->version = SAVEFILE_VERSION;
  h->size = SAVEFILE_HEADER;
}

void savefile_write(const SaveHeader * h, uint8_t * buf) {
  memset(buf, 0, SAVEFILE_HEADER);
  memcpy(buf, SAVEFILE_MAGIC, 8);
  buf[8] = h->version & 0xFF; buf[9] = h->version >> 8;
  buf[10] = SAVEFILE_HEADER & 0xFF; buf[11] = SAVEFILE_HEADER >> 8;
  memcpy(buf + 12, h->appver, 3);
  buf[15] = h->style;
  buf[16] = h->target & 0xFF; buf[17] = h->target >> 8;
  buf[18] = h->table;
}

int savefile_read(const uint8_t * buf, size_t len, SaveHeader * h) {
  if(len < SAVEFILE_MINHEADER || memcmp(buf, SAVEFILE_MAGIC, 8)) return 1;
  memset(h, 0, sizeof(SaveHeader));
  h->version = buf[8] | (buf[9] << 8);
  h->size = buf[10] | (buf[11] << 8);
  if(h->version > SAVEFILE_VERSION) return 2;
  if(h->size < SAVEFILE_MINHEADER || h->size > len) return 1;
  memcpy(h->appver, buf + 12, 3);
  h->style = buf[15];
  h->target = buf[16] | (buf[17] << 8);
  h->table = buf[18];
  return 0;
}

#include "test.h"
#include "savefile.h"

#include <stdlib.h>
#include <string.h>

void test_savefile(void) {
  Board b, c;
  CHECK(board_init(&b, 6, 5, 20, 400, 300) == 0);
  board_scatter(&b, 7, 0, 0, b.width, b.height);
  SaveHeader h, r;
  savefile_init(&h);
  h.appver[0] = 1; h.appver[1] = 2; h.appver[2] = 3;
  h.style = 1;
  h.target = 500;
  h.table = 7;
  size_t len = savefile_size(&b);
  uint8_t * buf = malloc(len);
  savefile_write(&h, &b, buf);
  CHECK(memcmp(buf, "3DSJGSAW", 8) == 0);
  CHECK(buf[8] == SAVEFILE_VERSION && buf[9] == 0 && buf[10] == SAVEFILE_HEADER && buf[11] == 0);
  int zero = 1;
  for(int i = SAVEFILE_MINHEADER; i < SAVEFILE_HEADER; i++) if(buf[i]) zero = 0;
  CHECK(zero);
  CHECK(memcmp(buf + SAVEFILE_HEADER, "JIGS", 4) == 0);

  CHECK(savefile_readheader(buf, len, &r) == SAVEFILE_OK);
  CHECK(r.version == SAVEFILE_VERSION && r.size == SAVEFILE_HEADER);
  CHECK(r.appver[0] == 1 && r.appver[1] == 2 && r.appver[2] == 3);
  CHECK(r.style == 1 && r.target == 500 && r.table == 7);

  // Peek works from just the start of the file, like the menu reads it
  SaveInfo info;
  CHECK(savefile_peek(buf, 256 < len ? 256 : len, &info) == SAVEFILE_OK);
  CHECK(info.pieces == 30 && info.groups == 30 && info.header.target == 500);

  // Board round trip
  CHECK(board_init(&c, 6, 5, 20, 400, 300) == 0);
  CHECK(savefile_readboard(buf, len, &r, &c) == SAVEFILE_OK);
  float x1, y1, x2, y2;
  board_piece_center(&b, 11, &x1, &y1);
  board_piece_center(&c, 11, &x2, &y2);
  CHECKF(x1, x2);
  CHECKF(y1, y2);
  CHECK(savefile_readboard(buf, len - 1, &r, &c) == SAVEFILE_BAD);
  board_free(&c);

  // Too short, bad magic, size past the end, newer version
  CHECK(savefile_readheader(buf, SAVEFILE_MINHEADER - 1, &r) == SAVEFILE_BAD);
  buf[0] = 'X';
  CHECK(savefile_readheader(buf, len, &r) == SAVEFILE_BAD);
  buf[0] = '3';
  CHECK(savefile_readheader(buf, 40, &r) == SAVEFILE_BAD);
  buf[8] = SAVEFILE_VERSION + 1;
  CHECK(savefile_readheader(buf, len, &r) == SAVEFILE_NEWER);
  CHECK(savefile_peek(buf, len, &info) == SAVEFILE_NEWER);
  // Old format (u32 prefix + "JIGS") isn't a save
  uint8_t old[32] = { 0xF4, 0x01, 0, 0, 'J', 'I', 'G', 'S' };
  CHECK(savefile_readheader(old, sizeof(old), &r) == SAVEFILE_BAD);
  free(buf);
  board_free(&b);
}

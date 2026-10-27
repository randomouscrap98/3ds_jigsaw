#include "test.h"
#include "savefile.h"

#include <stdlib.h>
#include <string.h>

void test_savefile(void) {
  Board b, c;
  CHECK(test_board_grid(&b, 6, 5, 20, 400, 300) == 0);
  board_scatter(&b, 7, 0, 0, b.width, b.height);
  SaveHeader h, r;
  savefile_init(&h);
  h.appver[0] = 1; h.appver[1] = 2; h.appver[2] = 3;
  h.style = 1;
  h.target = 500;
  h.table = 7;
  h.seconds = 0x01020304;
  h.music = SAVEFILE_MUSIC_SHUFFLE;
  size_t len = savefile_size(&b);
  uint8_t * buf = malloc(len);
  savefile_write(&h, &b, buf);
  CHECK(memcmp(buf, "3DSJGSAW", 8) == 0);
  CHECK(buf[8] == SAVEFILE_VERSION && buf[9] == 0 && buf[10] == SAVEFILE_HEADER && buf[11] == 0);
  int zero = 1;
  for(int i = 24; i < SAVEFILE_HEADER; i++) if(buf[i]) zero = 0;
  CHECK(zero);
  CHECK(buf[23] == SAVEFILE_MUSIC_SHUFFLE);
  CHECK(memcmp(buf + SAVEFILE_HEADER, "JIGS", 4) == 0);

  CHECK(savefile_readheader(buf, len, &r) == SAVEFILE_OK);
  CHECK(r.version == SAVEFILE_VERSION && r.size == SAVEFILE_HEADER);
  CHECK(r.appver[0] == 1 && r.appver[1] == 2 && r.appver[2] == 3);
  CHECK(r.style == 1 && r.target == 500 && r.table == 7 && r.seconds == 0x01020304);
  CHECK(buf[19] == 4 && buf[22] == 1);
  CHECK(r.music == SAVEFILE_MUSIC_SHUFFLE);
  // Saves from before the music field (byte 23 zero) play in order; junk values too
  buf[23] = 0;
  CHECK(savefile_readheader(buf, len, &r) == SAVEFILE_OK && r.music == SAVEFILE_MUSIC_ORDER);
  buf[23] = 200;
  CHECK(savefile_readheader(buf, len, &r) == SAVEFILE_OK && r.music == SAVEFILE_MUSIC_ORDER);
  buf[23] = SAVEFILE_MUSIC_SHUFFLE;

  // Peek works from just the start of the file, like the menu reads it
  SaveInfo info;
  CHECK(savefile_peek(buf, 256 < len ? 256 : len, &info) == SAVEFILE_OK);
  CHECK(info.pieces == 30 && info.groups == 30 && info.header.target == 500);

  // Board round trip
  CHECK(test_board_grid(&c, 6, 5, 20, 400, 300) == 0);
  CHECK(savefile_readboard(buf, len, &r, &c) == SAVEFILE_OK);
  float x1, y1, x2, y2;
  board_piece_center(&b, 11, &x1, &y1);
  board_piece_center(&c, 11, &x2, &y2);
  CHECKF(x1, x2);
  CHECKF(y1, y2);
  CHECK(savefile_readboard(buf, len - 1, &r, &c) == SAVEFILE_BAD);
  test_board_free(&c);

  // Too short, bad magic, size past the end, newer version
  CHECK(savefile_readheader(buf, SAVEFILE_MINHEADER - 1, &r) == SAVEFILE_BAD);
  buf[0] = 'X';
  CHECK(savefile_readheader(buf, len, &r) == SAVEFILE_BAD);
  buf[0] = '3';
  CHECK(savefile_readheader(buf, 40, &r) == SAVEFILE_BAD);
  buf[8] = SAVEFILE_VERSION + 1;
  CHECK(savefile_readheader(buf, len, &r) == SAVEFILE_NEWER);
  CHECK(savefile_peek(buf, len, &info) == SAVEFILE_NEWER);
  buf[8] = SAVEFILE_VERSION;

  // Cut seed: 0 means "from the name"; a stored one wins (a renamed picture keeps its cut)
  CHECK(savefile_readheader(buf, len, &r) == SAVEFILE_OK && r.seed == 0);
  CHECK(savefile_seedfor(&r, "Cat.jpg") == savefile_cutseed("Cat.jpg", 500, 1));
  CHECK(savefile_cutseed("Cat.jpg", 500, 1) != savefile_cutseed("Dog.jpg", 500, 1));
  size_t slen;
  uint8_t * sbuf = savefile_withseed(buf, len, &r, 0xA1B2C3D4, &slen);
  CHECK(sbuf && slen == len && savefile_readheader(sbuf, slen, &r) == SAVEFILE_OK && r.seed == 0xA1B2C3D4);
  CHECK(savefile_seedfor(&r, "Dog.jpg") == 0xA1B2C3D4 && r.target == 500 && r.music == SAVEFILE_MUSIC_SHUFFLE);
  CHECK(test_board_grid(&c, 6, 5, 20, 400, 300) == 0);
  CHECK(savefile_readboard(sbuf, slen, &r, &c) == SAVEFILE_OK);
  test_board_free(&c);
  free(sbuf);
  // A header too short for the seed grows to the full size, board intact
  size_t shortlen = len - (SAVEFILE_HEADER - 30);
  uint8_t * shortbuf = malloc(shortlen);
  memcpy(shortbuf, buf, 30);
  memcpy(shortbuf + 30, buf + SAVEFILE_HEADER, len - SAVEFILE_HEADER);
  shortbuf[10] = 30;
  CHECK(savefile_readheader(shortbuf, shortlen, &r) == SAVEFILE_OK && r.size == 30 && r.seed == 0);
  sbuf = savefile_withseed(shortbuf, shortlen, &r, 77, &slen);
  CHECK(sbuf && slen == len && savefile_readheader(sbuf, slen, &r) == SAVEFILE_OK);
  CHECK(r.size == SAVEFILE_HEADER && r.seed == 77 && r.seconds == 0x01020304);
  CHECK(test_board_grid(&c, 6, 5, 20, 400, 300) == 0);
  CHECK(savefile_readboard(sbuf, slen, &r, &c) == SAVEFILE_OK);
  test_board_free(&c);
  free(sbuf);
  free(shortbuf);
  // Written and read back
  h.seed = 0x12345678;
  savefile_write(&h, &b, buf);
  CHECK(savefile_readheader(buf, len, &r) == SAVEFILE_OK && r.seed == 0x12345678 && buf[30] == 0x78);
  // Overlay (byte 34)
  CHECK(r.overlay == 0);
  h.overlay = 1;
  savefile_write(&h, &b, buf);
  CHECK(buf[34] == 1 && savefile_readheader(buf, len, &r) == SAVEFILE_OK && r.overlay == 1);

  // Old format (u32 prefix + "JIGS") isn't a save
  uint8_t old[32] = { 0xF4, 0x01, 0, 0, 'J', 'I', 'G', 'S' };
  CHECK(savefile_readheader(old, sizeof(old), &r) == SAVEFILE_BAD);
  free(buf);
  test_board_free(&b);
}

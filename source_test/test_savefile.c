#include "test.h"
#include "savefile.h"

#include <string.h>

void test_savefile(void) {
  SaveHeader h, r;
  savefile_init(&h);
  h.appver[0] = 1; h.appver[1] = 2; h.appver[2] = 3;
  h.style = 1;
  h.target = 500;
  h.table = 7;
  uint8_t buf[SAVEFILE_HEADER + 8];
  memset(buf, 0xAA, sizeof(buf));
  savefile_write(&h, buf);
  CHECK(memcmp(buf, "3DSJGSAW", 8) == 0);
  CHECK(buf[8] == SAVEFILE_VERSION && buf[9] == 0 && buf[10] == SAVEFILE_HEADER && buf[11] == 0);
  int zero = 1;
  for(int i = SAVEFILE_MINHEADER; i < SAVEFILE_HEADER; i++) if(buf[i]) zero = 0;
  CHECK(zero);
  CHECK(buf[SAVEFILE_HEADER] == 0xAA);  // Writes exactly the header

  CHECK(savefile_read(buf, SAVEFILE_HEADER, &r) == 0);
  CHECK(r.version == SAVEFILE_VERSION && r.size == SAVEFILE_HEADER);
  CHECK(r.appver[0] == 1 && r.appver[1] == 2 && r.appver[2] == 3);
  CHECK(r.style == 1 && r.target == 500 && r.table == 7);

  // Too short, bad magic, size past the end, newer version
  CHECK(savefile_read(buf, SAVEFILE_HEADER - 1, &r) == 1);
  buf[0] = 'X';
  CHECK(savefile_read(buf, sizeof(buf), &r) == 1);
  buf[0] = '3';
  buf[10] = 200;
  CHECK(savefile_read(buf, sizeof(buf), &r) == 1);
  buf[10] = SAVEFILE_HEADER;
  buf[8] = SAVEFILE_VERSION + 1;
  CHECK(savefile_read(buf, sizeof(buf), &r) == 2);
  // Old format (u32 prefix + "JIGS") isn't a save
  uint8_t old[32] = { 0xF4, 0x01, 0, 0, 'J', 'I', 'G', 'S' };
  CHECK(savefile_read(old, sizeof(old), &r) == 1);
}

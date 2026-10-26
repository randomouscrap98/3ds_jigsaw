#include "test.h"
#include "storage.h"

#include <string.h>

void test_storage(void) {
  const char * n = "Wheat Field.jpg";
  CHECK(storage_iscache(n, "Wheat Field.jpg_48.bin"));
  CHECK(storage_iscache(n, "Wheat Field.jpg_500_s2.bin"));
  // Not this picture's caches
  CHECK(!storage_iscache(n, "Wheat Field.jpg.bin"));
  CHECK(!storage_iscache(n, "Wheat Field.jpg_48.bin.tmp"));
  CHECK(!storage_iscache(n, "Wheat Field.jpg_s1.bin"));
  CHECK(!storage_iscache(n, "Wheat Field.jpg_48_s.bin"));
  CHECK(!storage_iscache(n, "Wheat Field.jpg_48_x1.bin"));
  CHECK(!storage_iscache(n, "Wheat Field.jpg_1.png_48.bin"));   // Another picture
  CHECK(!storage_iscache(n, "Wheat Field.jpeg_48.bin"));
  CHECK(!storage_iscache(n, "Wheat Field.jp_48.bin"));
  // Everything storage_cachepath makes is recognized
  int all = 1;
  uint32_t counts[4] = { 48, 108, 252, 500 };
  for(int c = 0; c < 4; c++) for(uint8_t st = 0; st < 3; st++) {
    char path[STORAGE_MAXPATH];
    storage_cachepath(n, counts[c], st, path);
    if(!storage_iscache(n, strrchr(path, '/') + 1)) all = 0;
  }
  CHECK(all);
}

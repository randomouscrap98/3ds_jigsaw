#include "test.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Upload names: safe, and never clobbering an existing picture
static void test_storage_names(void) {
  char out[STORAGE_MAXBASE + 1];
  storage_cleanname("Cat photo.jpg", out, sizeof(out));
  CHECK(!strcmp(out, "Cat photo"));
  storage_cleanname("C:\\Users\\me\\IMG_0001.HEIC", out, sizeof(out));
  CHECK(!strcmp(out, "IMG_0001"));
  storage_cleanname("../../evil/../x.png", out, sizeof(out));
  CHECK(!strcmp(out, "x"));
  storage_cleanname("  s\xc3\xa9rie: <1>?.jpg", out, sizeof(out));
  CHECK(!strcmp(out, "s__rie_ _1__"));
  storage_cleanname("", out, sizeof(out));
  CHECK(!strcmp(out, "picture"));
  storage_cleanname(".jpg", out, sizeof(out));
  CHECK(!strcmp(out, "picture"));
  char longname[200];
  memset(longname, 'a', sizeof(longname) - 1);
  longname[sizeof(longname) - 1] = 0;
  storage_cleanname(longname, out, sizeof(out));
  CHECK(strlen(out) == STORAGE_MAXBASE);
  // Free names in a scratch folder
  char dir[] = "/tmp/jigsaw_test_XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  char name[128], path[256];
  CHECK(storage_freename(dir, "Cat", ".jpg", name, sizeof(name)) == 0 && !strcmp(name, "Cat.jpg"));
  for(int i = 0; i < 2; i++) {
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE * f = fopen(path, "wb");
    if(f) fclose(f);
    CHECK(storage_freename(dir, "Cat", ".jpg", name, sizeof(name)) == 0);
  }
  CHECK(!strcmp(name, "Cat (3).jpg"));
  snprintf(path, sizeof(path), "%s/Cat.jpg", dir); remove(path);
  snprintf(path, sizeof(path), "%s/Cat (2).jpg", dir); remove(path);
  rmdir(dir);
}

void test_storage(void) {
  test_storage_names();
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
  CHECK(storage_iscache(n, "Wheat Field.jpg_500_s3_w2.bin"));
  CHECK(storage_iscachetmp(n, "Wheat Field.jpg_500_s3_w2.bin.tmp"));
  CHECK(storage_iscachetmp(n, "Wheat Field.jpg_48.bin.tmp"));
  CHECK(!storage_iscachetmp(n, "Wheat Field.jpg_48.bin"));
  CHECK(!storage_iscachetmp(n, "Wheat Field.jpg.sav.tmp"));
  CHECK(!storage_iscachetmp(n, "Other.jpg_48.bin.tmp"));
  CHECK(!storage_iscachetmp(n, ".tmp"));
  CHECK(storage_iscache(n, "Wheat Field.jpg_100_w1_g1.bin"));
  CHECK(!storage_iscache(n, "Wheat Field.jpg_100_g1.bin"));
  CHECK(!storage_iscache(n, "Wheat Field.jpg_100_w.bin"));
  CHECK(!storage_iscache(n, "Wheat Field.jpg_100_w1_s1.bin"));
  // Old names are unchanged
  char old[STORAGE_MAXPATH];
  storage_cachepath(n, 500, 2, 0, 0, old);
  CHECK(!strcmp(strrchr(old, '/') + 1, "Wheat Field.jpg_500_s2.bin"));
  storage_cachepath(n, 48, 0, 0, 0, old);
  CHECK(!strcmp(strrchr(old, '/') + 1, "Wheat Field.jpg_48.bin"));
  // Everything storage_cachepath makes is recognized
  int all = 1;
  uint32_t counts[4] = { 48, 108, 252, 500 };
  for(int c = 0; c < 4; c++) for(uint8_t st = 0; st < 4; st++) for(uint8_t w = 0; w < 3; w++) for(uint8_t g = 0; g < 2; g++) {
    char path[STORAGE_MAXPATH];
    storage_cachepath(n, counts[c], st, w, g, path);
    if(!storage_iscache(n, strrchr(path, '/') + 1)) all = 0;
  }
  CHECK(all);
}

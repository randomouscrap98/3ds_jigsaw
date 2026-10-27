#include "test.h"
#include "picfiles.h"
#include "savefile.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void picfiles_touch(const char * dir, const char * name, const void * data, size_t len) {
  char path[STORAGE_MAXPATH];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  FILE * f = fopen(path, "wb");
  if(f) {
    fwrite(data, 1, len, f);
    fclose(f);
  }
}

static int picfiles_has(const char * dir, const char * name) {
  char path[STORAGE_MAXPATH];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  struct stat st;
  return stat(path, &st) == 0;
}

static void picfiles_rm(const char * dir, const char * name) {
  char path[STORAGE_MAXPATH];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  remove(path);
}

// Seed in the save at saves/name (0 if unreadable)
static uint32_t picfiles_seed(const char * dir, const char * name, int * boardok) {
  char path[STORAGE_MAXPATH];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  uint8_t * buf;
  size_t len;
  if(storage_readfile(path, &buf, &len)) return 0;
  SaveHeader h;
  uint32_t seed = savefile_readheader(buf, len, &h) == SAVEFILE_OK ? h.seed : 0;
  Board c;
  *boardok = test_board_grid(&c, 6, 5, 20, 400, 300) == 0 && savefile_readboard(buf, len, &h, &c) == SAVEFILE_OK;
  test_board_free(&c);
  free(buf);
  return seed;
}

void test_picfiles(void) {
  char root[] = "/tmp/jigsaw_ren_XXXXXX";
  CHECK(mkdtemp(root) != NULL);
  char puz[256], sav[256], cache[256];
  snprintf(puz, sizeof(puz), "%s/puzzles", root);
  snprintf(sav, sizeof(sav), "%s/saves", root);
  snprintf(cache, sizeof(cache), "%s/cache", root);
  mkdir(puz, 0777);
  mkdir(sav, 0777);
  mkdir(cache, 0777);
  PicDirs d = { puz, sav, cache };

  // A puzzle with a save from before seeds were stored, some caches, a cut-short cache
  // write, and a neighbour whose name starts the same
  picfiles_touch(puz, "Old.jpg", "pic", 3);
  picfiles_touch(puz, "Other.jpg", "pic", 3);
  picfiles_touch(puz, "Old.jpg2.jpg", "pic", 3);
  Board b;
  CHECK(test_board_grid(&b, 6, 5, 20, 400, 300) == 0);
  SaveHeader h;
  savefile_init(&h);
  h.target = 100;
  h.style = 1;
  size_t len = savefile_size(&b);
  uint8_t * buf = malloc(len);
  savefile_write(&h, &b, buf);
  picfiles_touch(sav, "Old.jpg.sav", buf, len);
  picfiles_touch(cache, "Old.jpg_100_s1.bin", "c1", 2);
  picfiles_touch(cache, "Old.jpg_50.bin", "c2", 2);
  picfiles_touch(cache, "Old.jpg_100_s1.bin.tmp", "t", 1);
  picfiles_touch(cache, "Old.jpg2.jpg_100.bin", "c3", 2);
  char newname[128];

  // Taken by a picture, or by a leftover save: nothing changes
  CHECK(picfiles_rename(&d, "Old.jpg", "Other", newname, sizeof(newname)) == PICFILES_TAKEN);
  picfiles_touch(sav, "Ghost.jpg.sav", buf, len);
  CHECK(picfiles_rename(&d, "Old.jpg", "Ghost", newname, sizeof(newname)) == PICFILES_TAKEN);
  CHECK(picfiles_has(puz, "Old.jpg") && picfiles_has(sav, "Old.jpg.sav") && picfiles_has(cache, "Old.jpg_50.bin"));
  // Same name: fine, nothing to do
  CHECK(picfiles_rename(&d, "Old.jpg", "Old", newname, sizeof(newname)) == PICFILES_OK && !strcmp(newname, "Old.jpg"));

  // The rename: everything follows, the save keeps the old name's seed
  int boardok = 0;
  CHECK(picfiles_rename(&d, "Old.jpg", "New name", newname, sizeof(newname)) == PICFILES_OK);
  CHECK(!strcmp(newname, "New name.jpg"));
  CHECK(!picfiles_has(puz, "Old.jpg") && picfiles_has(puz, "New name.jpg"));
  CHECK(!picfiles_has(sav, "Old.jpg.sav"));
  uint32_t seed = savefile_cutseed("Old.jpg", 100, 1);
  CHECK(picfiles_seed(sav, "New name.jpg.sav", &boardok) == seed && boardok);
  CHECK(picfiles_has(cache, "New name.jpg_100_s1.bin") && picfiles_has(cache, "New name.jpg_50.bin"));
  CHECK(!picfiles_has(cache, "Old.jpg_100_s1.bin") && !picfiles_has(cache, "Old.jpg_50.bin"));
  CHECK(!picfiles_has(cache, "Old.jpg_100_s1.bin.tmp") && !picfiles_has(cache, "New name.jpg_100_s1.bin.tmp"));
  CHECK(picfiles_has(puz, "Old.jpg2.jpg") && picfiles_has(cache, "Old.jpg2.jpg_100.bin"));
  // Again: the stored seed stays
  CHECK(picfiles_rename(&d, "New name.jpg", "Third", newname, sizeof(newname)) == PICFILES_OK);
  CHECK(picfiles_seed(sav, "Third.jpg.sav", &boardok) == seed && boardok);
  // Only the case changes (allowed even though the "new" name exists on a case-blind card)
  CHECK(picfiles_rename(&d, "Third.jpg", "THIRD", newname, sizeof(newname)) == PICFILES_OK);
  CHECK(picfiles_has(puz, "THIRD.jpg") && picfiles_has(sav, "THIRD.jpg.sav"));
  // No save: just the picture
  CHECK(picfiles_rename(&d, "Other.jpg", "Plain", newname, sizeof(newname)) == PICFILES_OK);
  CHECK(picfiles_has(puz, "Plain.jpg") && !picfiles_has(sav, "Plain.jpg.sav"));

  // Size: picture + save + caches (3 + savefile + 2 + 2 bytes)
  CHECK(picfiles_size(&d, "THIRD.jpg") == 3 + len + 4);
  CHECK(picfiles_size(&d, "Plain.jpg") == 3);
  CHECK(picfiles_size(&d, "Nothing.jpg") == 0);
  // Delete: picture, save, caches, leftover temp; the neighbour with the longer name stays
  picfiles_touch(puz, "Gone.jpg", "pic", 3);
  picfiles_touch(sav, "Gone.jpg.sav", buf, len);
  picfiles_touch(cache, "Gone.jpg_100.bin", "c", 1);
  picfiles_touch(cache, "Gone.jpg_100_o1.bin", "c", 1);
  picfiles_touch(cache, "Gone.jpg_100.bin.tmp", "t", 1);
  picfiles_touch(cache, "Gone.jpg2_100.bin", "c", 1);
  CHECK(picfiles_size(&d, "Gone.jpg") == 3 + len + 3);
  CHECK(picfiles_delete(&d, "Gone.jpg") == PICFILES_OK);
  CHECK(!picfiles_has(puz, "Gone.jpg") && !picfiles_has(sav, "Gone.jpg.sav"));
  CHECK(!picfiles_has(cache, "Gone.jpg_100.bin") && !picfiles_has(cache, "Gone.jpg_100_o1.bin"));
  CHECK(!picfiles_has(cache, "Gone.jpg_100.bin.tmp") && picfiles_has(cache, "Gone.jpg2_100.bin"));
  picfiles_rm(cache, "Gone.jpg2_100.bin");
  // No picture: fails, the save stays
  CHECK(picfiles_delete(&d, "Ghost.jpg") == PICFILES_FAILED && picfiles_has(sav, "Ghost.jpg.sav"));

  const char * left[][2] = {
    { puz, "THIRD.jpg" }, { puz, "Plain.jpg" }, { puz, "Old.jpg2.jpg" }, { sav, "THIRD.jpg.sav" },
    { sav, "Ghost.jpg.sav" }, { cache, "THIRD.jpg_100_s1.bin" }, { cache, "THIRD.jpg_50.bin" },
    { cache, "Old.jpg2.jpg_100.bin" },
  };
  for(size_t i = 0; i < sizeof(left) / sizeof(left[0]); i++) {
    CHECK(picfiles_has(left[i][0], left[i][1]));
    picfiles_rm(left[i][0], left[i][1]);
  }
  CHECK(rmdir(puz) == 0 && rmdir(sav) == 0 && rmdir(cache) == 0 && rmdir(root) == 0);
  free(buf);
  test_board_free(&b);
}

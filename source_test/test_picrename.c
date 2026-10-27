#include "test.h"
#include "picrename.h"
#include "savefile.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void picrename_touch(const char * dir, const char * name, const void * data, size_t len) {
  char path[STORAGE_MAXPATH];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  FILE * f = fopen(path, "wb");
  if(f) {
    fwrite(data, 1, len, f);
    fclose(f);
  }
}

static int picrename_has(const char * dir, const char * name) {
  char path[STORAGE_MAXPATH];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  struct stat st;
  return stat(path, &st) == 0;
}

static void picrename_rm(const char * dir, const char * name) {
  char path[STORAGE_MAXPATH];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  remove(path);
}

// Seed in the save at saves/name (0 if unreadable)
static uint32_t picrename_seed(const char * dir, const char * name, int * boardok) {
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

void test_picrename(void) {
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
  picrename_touch(puz, "Old.jpg", "pic", 3);
  picrename_touch(puz, "Other.jpg", "pic", 3);
  picrename_touch(puz, "Old.jpg2.jpg", "pic", 3);
  Board b;
  CHECK(test_board_grid(&b, 6, 5, 20, 400, 300) == 0);
  SaveHeader h;
  savefile_init(&h);
  h.target = 100;
  h.style = 1;
  size_t len = savefile_size(&b);
  uint8_t * buf = malloc(len);
  savefile_write(&h, &b, buf);
  picrename_touch(sav, "Old.jpg.sav", buf, len);
  picrename_touch(cache, "Old.jpg_100_s1.bin", "c1", 2);
  picrename_touch(cache, "Old.jpg_50.bin", "c2", 2);
  picrename_touch(cache, "Old.jpg_100_s1.bin.tmp", "t", 1);
  picrename_touch(cache, "Old.jpg2.jpg_100.bin", "c3", 2);
  char newname[128];

  // Taken by a picture, or by a leftover save: nothing changes
  CHECK(picrename(&d, "Old.jpg", "Other", newname, sizeof(newname)) == PICRENAME_TAKEN);
  picrename_touch(sav, "Ghost.jpg.sav", buf, len);
  CHECK(picrename(&d, "Old.jpg", "Ghost", newname, sizeof(newname)) == PICRENAME_TAKEN);
  CHECK(picrename_has(puz, "Old.jpg") && picrename_has(sav, "Old.jpg.sav") && picrename_has(cache, "Old.jpg_50.bin"));
  // Same name: fine, nothing to do
  CHECK(picrename(&d, "Old.jpg", "Old", newname, sizeof(newname)) == PICRENAME_OK && !strcmp(newname, "Old.jpg"));

  // The rename: everything follows, the save keeps the old name's seed
  int boardok = 0;
  CHECK(picrename(&d, "Old.jpg", "New name", newname, sizeof(newname)) == PICRENAME_OK);
  CHECK(!strcmp(newname, "New name.jpg"));
  CHECK(!picrename_has(puz, "Old.jpg") && picrename_has(puz, "New name.jpg"));
  CHECK(!picrename_has(sav, "Old.jpg.sav"));
  uint32_t seed = savefile_cutseed("Old.jpg", 100, 1);
  CHECK(picrename_seed(sav, "New name.jpg.sav", &boardok) == seed && boardok);
  CHECK(picrename_has(cache, "New name.jpg_100_s1.bin") && picrename_has(cache, "New name.jpg_50.bin"));
  CHECK(!picrename_has(cache, "Old.jpg_100_s1.bin") && !picrename_has(cache, "Old.jpg_50.bin"));
  CHECK(!picrename_has(cache, "Old.jpg_100_s1.bin.tmp") && !picrename_has(cache, "New name.jpg_100_s1.bin.tmp"));
  CHECK(picrename_has(puz, "Old.jpg2.jpg") && picrename_has(cache, "Old.jpg2.jpg_100.bin"));
  // Again: the stored seed stays
  CHECK(picrename(&d, "New name.jpg", "Third", newname, sizeof(newname)) == PICRENAME_OK);
  CHECK(picrename_seed(sav, "Third.jpg.sav", &boardok) == seed && boardok);
  // Only the case changes (allowed even though the "new" name exists on a case-blind card)
  CHECK(picrename(&d, "Third.jpg", "THIRD", newname, sizeof(newname)) == PICRENAME_OK);
  CHECK(picrename_has(puz, "THIRD.jpg") && picrename_has(sav, "THIRD.jpg.sav"));
  // No save: just the picture
  CHECK(picrename(&d, "Other.jpg", "Plain", newname, sizeof(newname)) == PICRENAME_OK);
  CHECK(picrename_has(puz, "Plain.jpg") && !picrename_has(sav, "Plain.jpg.sav"));

  const char * left[][2] = {
    { puz, "THIRD.jpg" }, { puz, "Plain.jpg" }, { puz, "Old.jpg2.jpg" }, { sav, "THIRD.jpg.sav" },
    { sav, "Ghost.jpg.sav" }, { cache, "THIRD.jpg_100_s1.bin" }, { cache, "THIRD.jpg_50.bin" },
    { cache, "Old.jpg2.jpg_100.bin" },
  };
  for(size_t i = 0; i < sizeof(left) / sizeof(left[0]); i++) {
    CHECK(picrename_has(left[i][0], left[i][1]));
    picrename_rm(left[i][0], left[i][1]);
  }
  CHECK(rmdir(puz) == 0 && rmdir(sav) == 0 && rmdir(cache) == 0 && rmdir(root) == 0);
  free(buf);
  test_board_free(&b);
}

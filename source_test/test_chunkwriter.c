#include "test.h"
#include "chunkwriter.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Write len bytes in pieces of step through a ChunkWriter, read the file back
static int chunk_roundtrip(const char * path, size_t len, size_t step) {
  ChunkWriter c;
  if(chunkwriter_open(&c, path)) return 0;
  char * src = malloc(len + 1);
  for(size_t i = 0; i < len; i++) src[i] = (char)(i * 13 + i / 4096);
  size_t done = 0;
  while(done < len) {
    size_t room;
    char * dst = chunkwriter_space(&c, &room);
    if(!dst) { usleep(100); continue; }
    size_t n = len - done < step ? len - done : step;
    if(n > room) n = room;
    memcpy(dst, src + done, n);
    chunkwriter_wrote(&c, n);
    done += n;
  }
  int ok = !chunkwriter_close(&c);
  FILE * f = fopen(path, "rb");
  char * back = malloc(len + 1);
  size_t got = f ? fread(back, 1, len + 1, f) : 0;
  if(f) fclose(f);
  ok = ok && got == len && !memcmp(src, back, len);
  free(src);
  free(back);
  return ok;
}

void test_chunkwriter(void) {
  char dir[] = "/tmp/jigsaw_chunk_XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  char path[512];
  snprintf(path, sizeof(path), "%s/f", dir);
  CHECK(chunk_roundtrip(path, 0, 1000));
  CHECK(chunk_roundtrip(path, 100, 7));
  CHECK(chunk_roundtrip(path, CHUNKWRITER_SIZE, 16384));
  CHECK(chunk_roundtrip(path, 5 * CHUNKWRITER_SIZE + 3, 65537));
  // Abort: stops, file closed (caller removes it)
  ChunkWriter c;
  CHECK(chunkwriter_open(&c, path) == 0);
  size_t room;
  char * dst = chunkwriter_space(&c, &room);
  CHECK(dst && room == CHUNKWRITER_SIZE);
  memset(dst, 1, room);
  chunkwriter_wrote(&c, room);
  chunkwriter_abort(&c);
  // Can't open: error, nothing leaks
  char bad[600];
  snprintf(bad, sizeof(bad), "%s/missing/f", dir);
  CHECK(chunkwriter_open(&c, bad) == 1);
  remove(path);
  rmdir(dir);
}

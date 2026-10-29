#ifndef __HEADER_JIGSAW_CHUNKWRITER__
#define __HEADER_JIGSAW_CHUNKWRITER__

#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __3DS__
#include <3ds.h>
#else
#include <pthread.h>
#endif

// Streams a file to disk through two RAM buffers: the caller fills one while a background
// thread writes the other, so a slow SD write never stops the caller (the upload server
// keeps draining its socket). Buffers are written in order. If the thread can't be made,
// full buffers are written inline (also with -DCHUNKWRITER_NOTHREAD, for testing).

#define CHUNKWRITER_SIZE (1 << 20)
#define CHUNKWRITER_STACK (16 * 1024)

typedef struct {
  FILE * f;
  char * buf[2];
  size_t len[2];            // Bytes in each buffer (the writer zeroes it when done)
  int fill;                 // Buffer the caller is filling
  atomic_int full[2];       // Handed to the writer, not written yet
  atomic_int quit;          // 1 = write what's handed then stop, 2 = stop now
  atomic_int failed;        // A write failed (later buffers are dropped)
  int running;
#ifdef __3DS__
  Thread thread;
  LightEvent wake;          // Signalled on a handoff or quit
#else
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  int woken;
#endif
} ChunkWriter;

// Create path (truncating) and start the writer. Returns 0, or 1 (no memory / can't open).
int chunkwriter_open(ChunkWriter * c, const char * path);
// Room to fill: *room bytes at the result, or NULL if both buffers are still being
// written (try again later)
char * chunkwriter_space(ChunkWriter * c, size_t * room);
// n bytes were put at chunkwriter_space's pointer
void chunkwriter_wrote(ChunkWriter * c, size_t n);
// 1 if a write has failed (SD full or gone)
int chunkwriter_failed(ChunkWriter * c);
// Write the rest, wait, close. Returns 0 if everything reached the file.
int chunkwriter_close(ChunkWriter * c);
// Stop without writing the rest and close (the caller removes the file)
void chunkwriter_abort(ChunkWriter * c);

#endif

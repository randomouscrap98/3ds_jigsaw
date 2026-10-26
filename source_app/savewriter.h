#ifndef __HEADER_JIGSAW_SAVEWRITER__
#define __HEADER_JIGSAW_SAVEWRITER__

#include <3ds.h>

#include "storage.h"

#define SAVEWRITER_STACK (16 * 1024)

// Writes files on a background thread so a slow SD card doesn't stall the game. Holds at
// most one pending write: submitting again before it starts replaces it (latest wins).
// Writes go through storage_writefile, so they're still atomic.
typedef struct {
  Thread thread;
  LightLock lock;
  LightEvent wake;
  char path[STORAGE_MAXPATH];
  uint8_t * pending;      // Owned; NULL = nothing queued
  size_t pendinglen;
  volatile u8 busy;       // Worker is in the middle of a write
  volatile u8 quit;
  volatile u8 failed;     // The last write failed (cleared by the next good one)
  u8 running;             // Thread was created (otherwise writes happen inline)
} SaveWriter;

int savewriter_init(SaveWriter * sw);
// Stops the thread after finishing any queued write
void savewriter_free(SaveWriter * sw);
// Queue buf (malloc'd; ownership passes to the writer) to be written to path
void savewriter_submit(SaveWriter * sw, const char * path, uint8_t * buf, size_t len);
// Block until nothing is queued or being written
void savewriter_flush(SaveWriter * sw);
// 1 if the last write failed (SD card full or missing)
int savewriter_failed(SaveWriter * sw);

#endif

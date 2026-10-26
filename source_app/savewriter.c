#include "savewriter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void savewriter_thread(void * arg) {
  SaveWriter * sw = arg;
  char path[STORAGE_MAXPATH];
  for(;;) {
    LightEvent_Wait(&sw->wake);
    for(;;) {
      LightLock_Lock(&sw->lock);
      uint8_t * buf = sw->pending;
      size_t len = sw->pendinglen;
      if(buf) {
        memcpy(path, sw->path, sizeof(path));
        sw->pending = NULL;
        sw->busy = 1;
      }
      LightLock_Unlock(&sw->lock);
      if(!buf) break;
      sw->failed = storage_writefile(path, buf, len) != 0;
      free(buf);
      sw->busy = 0;
    }
    if(sw->quit) return;
  }
}

int savewriter_init(SaveWriter * sw) {
  memset(sw, 0, sizeof(SaveWriter));
  LightLock_Init(&sw->lock);
  LightEvent_Init(&sw->wake, RESET_ONESHOT);
  // Lower priority than the main thread (bigger number = lower), same core
  s32 prio = 0x30;
  svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  sw->thread = threadCreate(savewriter_thread, sw, SAVEWRITER_STACK, prio + 1, -2, false);
  sw->running = sw->thread != NULL;
  return sw->running ? 0 : 1;
}

void savewriter_submit(SaveWriter * sw, const char * path, uint8_t * buf, size_t len) {
  if(!sw || !sw->running) {
    if(sw) sw->failed = storage_writefile(path, buf, len) != 0;
    else storage_writefile(path, buf, len);
    free(buf);
    return;
  }
  LightLock_Lock(&sw->lock);
  // A different file still waiting must not be dropped; write it out first (rare)
  if(sw->pending && strcmp(sw->path, path) != 0) {
    LightLock_Unlock(&sw->lock);
    savewriter_flush(sw);
    LightLock_Lock(&sw->lock);
  }
  free(sw->pending);
  snprintf(sw->path, sizeof(sw->path), "%s", path);
  sw->pending = buf;
  sw->pendinglen = len;
  LightLock_Unlock(&sw->lock);
  LightEvent_Signal(&sw->wake);
}

void savewriter_flush(SaveWriter * sw) {
  if(!sw || !sw->running) return;
  for(;;) {
    LightLock_Lock(&sw->lock);
    int idle = !sw->pending && !sw->busy;
    LightLock_Unlock(&sw->lock);
    if(idle) return;
    LightEvent_Signal(&sw->wake);
    svcSleepThread(2 * 1000 * 1000);  // 2ms
  }
}

void savewriter_free(SaveWriter * sw) {
  if(sw->running) {
    savewriter_flush(sw);
    sw->quit = 1;
    LightEvent_Signal(&sw->wake);
    threadJoin(sw->thread, U64_MAX);
    threadFree(sw->thread);
  }
  free(sw->pending);
  memset(sw, 0, sizeof(SaveWriter));
}

int savewriter_failed(SaveWriter * sw) {
  return sw && sw->failed;
}

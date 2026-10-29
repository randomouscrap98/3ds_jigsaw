#include "chunkwriter.h"

#include <stdlib.h>
#include <string.h>

// Writer sleeps until signalled (a signal before the wait isn't lost)
static void chunkwriter_signal(ChunkWriter * c) {
  if(!c->running) return;
#ifdef __3DS__
  LightEvent_Signal(&c->wake);
#else
  pthread_mutex_lock(&c->lock);
  c->woken = 1;
  pthread_cond_signal(&c->cond);
  pthread_mutex_unlock(&c->lock);
#endif
}

static void chunkwriter_wait(ChunkWriter * c) {
#ifdef __3DS__
  LightEvent_Wait(&c->wake);
#else
  pthread_mutex_lock(&c->lock);
  while(!c->woken) pthread_cond_wait(&c->cond, &c->lock);
  c->woken = 0;
  pthread_mutex_unlock(&c->lock);
#endif
}

// Write buffer i (if nothing failed yet) and give it back
static void chunkwriter_put(ChunkWriter * c, int i) {
  if(!atomic_load(&c->failed) && fwrite(c->buf[i], 1, c->len[i], c->f) != c->len[i]) atomic_store(&c->failed, 1);
  c->len[i] = 0;
  atomic_store(&c->full[i], 0);
}

static void chunkwriter_loop(ChunkWriter * c) {
  int next = 0;
  for(;;) {
    int q = atomic_load(&c->quit);
    if(q == 2) return;
    if(atomic_load(&c->full[next])) {
      chunkwriter_put(c, next);
      next ^= 1;
    } else if(q) return;
    else chunkwriter_wait(c);
  }
}

#ifdef __3DS__
static void chunkwriter_thread(void * arg) {
  chunkwriter_loop(arg);
}
#else
static void * chunkwriter_thread(void * arg) {
  chunkwriter_loop(arg);
  return NULL;
}
#endif

int chunkwriter_open(ChunkWriter * c, const char * path) {
  memset(c, 0, sizeof(ChunkWriter));
  c->buf[0] = malloc(CHUNKWRITER_SIZE);
  c->buf[1] = malloc(CHUNKWRITER_SIZE);
  c->f = c->buf[0] && c->buf[1] ? fopen(path, "wb") : NULL;
  if(!c->f) {
    free(c->buf[0]);
    free(c->buf[1]);
    return 1;
  }
#ifndef CHUNKWRITER_NOTHREAD
#ifdef __3DS__
  // Just below the caller (the receiver wins when both can run), same core
  LightEvent_Init(&c->wake, RESET_ONESHOT);
  s32 prio = 0x30;
  svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  c->thread = threadCreate(chunkwriter_thread, c, CHUNKWRITER_STACK, prio < 0x3F ? prio + 1 : prio, -2, false);
  c->running = c->thread != NULL;
#else
  pthread_mutex_init(&c->lock, NULL);
  pthread_cond_init(&c->cond, NULL);
  c->running = !pthread_create(&c->thread, NULL, chunkwriter_thread, c);
  if(!c->running) {
    pthread_cond_destroy(&c->cond);
    pthread_mutex_destroy(&c->lock);
  }
#endif
#endif
  return 0;
}

char * chunkwriter_space(ChunkWriter * c, size_t * room) {
  if(atomic_load(&c->full[c->fill])) return NULL;
  *room = CHUNKWRITER_SIZE - c->len[c->fill];
  return c->buf[c->fill] + c->len[c->fill];
}

void chunkwriter_wrote(ChunkWriter * c, size_t n) {
  int i = c->fill;
  c->len[i] += n;
  if(c->len[i] < CHUNKWRITER_SIZE) return;
  atomic_store(&c->full[i], 1);
  c->fill ^= 1;
  if(!c->running) chunkwriter_put(c, i);
  chunkwriter_signal(c);
}

int chunkwriter_failed(ChunkWriter * c) {
  return atomic_load(&c->failed);
}

static void chunkwriter_end(ChunkWriter * c, int quit) {
  if(c->running) {
    atomic_store(&c->quit, quit);
    chunkwriter_signal(c);
#ifdef __3DS__
    threadJoin(c->thread, U64_MAX);
    threadFree(c->thread);
#else
    pthread_join(c->thread, NULL);
    pthread_cond_destroy(&c->cond);
    pthread_mutex_destroy(&c->lock);
#endif
    c->running = 0;
  }
  if(fclose(c->f)) atomic_store(&c->failed, 1);
  c->f = NULL;
  free(c->buf[0]);
  free(c->buf[1]);
  c->buf[0] = c->buf[1] = NULL;
}

int chunkwriter_close(ChunkWriter * c) {
  int i = c->fill;
  if(c->len[i]) {
    atomic_store(&c->full[i], 1);
    if(!c->running) chunkwriter_put(c, i);
  }
  chunkwriter_end(c, 1);
  return atomic_load(&c->failed);
}

void chunkwriter_abort(ChunkWriter * c) {
  chunkwriter_end(c, 2);
}

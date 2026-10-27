#include "music.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void * music_malloc(size_t sz, void * u) {
  return sz > MUSIC_MAXALLOC ? NULL : malloc(sz);
}

static void * music_realloc(void * p, size_t sz, void * u) {
  return sz > MUSIC_MAXALLOC ? NULL : realloc(p, sz);
}

static void music_mfree(void * p, void * u) {
  free(p);
}

// The one dr_mp3 implementation in the app
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

static const drmp3_allocation_callbacks music_alloc = {
  .onMalloc = music_malloc, .onRealloc = music_realloc, .onFree = music_mfree,
};

static int music_cmp(const void * a, const void * b) {
  return strcasecmp(*(char * const *)a, *(char * const *)b);
}

// Sorted .mp3 names in MUSIC_DIR. Returns count; *out = NULL if none.
static uint32_t music_list(char *** out) {
  *out = NULL;
  DIR * d = opendir(MUSIC_DIR);
  if(!d) return 0;
  char ** names = NULL;
  uint32_t n = 0, cap = 0;
  struct dirent * de;
  while((de = readdir(d))) {
    const char * dot = strrchr(de->d_name, '.');
    if(!dot || strcasecmp(dot, ".mp3")) continue;
    if(n == cap) {
      uint32_t nc = cap ? cap * 2 : 16;
      char ** nn = realloc(names, sizeof(char *) * nc);
      if(!nn) break;
      names = nn;
      cap = nc;
    }
    if(!(names[n] = strdup(de->d_name))) break;
    n++;
  }
  closedir(d);
  if(n) qsort(names, n, sizeof(char *), music_cmp);
  else free(names);
  *out = n ? names : NULL;
  return n;
}

static void music_setname(Music * m, const char * name) {
  LightLock_Lock(&m->lock);
  snprintf(m->name, sizeof(m->name), "%s", name);
  LightLock_Unlock(&m->lock);
}

static int music_queued(Music * m) {
  for(int i = 0; i < MUSIC_BUFS; i++) {
    u8 s = m->wave[i].status;
    if(s == NDSP_WBUF_QUEUED || s == NDSP_WBUF_PLAYING) return 1;
  }
  return 0;
}

static void music_callback(void * data) {
  LightEvent_Signal(&((Music *)data)->wake);
}

static void music_thread(void * arg) {
  Music * m = arg;
  char ** files;
  uint32_t nfiles = music_list(&files);
  drmp3 * mp3 = malloc(sizeof(drmp3));
  uint32_t next = 0, fails = 0;  // fails = files in a row that gave no audio
  u64 trackframes = 0;
  int open = 0;
  char path[STORAGE_MAXPATH];
  while(!m->quit && mp3 && nfiles && fails < nfiles) {
    if(!open) {
      // Let the last track finish before changing rate / format
      while(!m->quit && music_queued(m)) LightEvent_Wait(&m->wake);
      if(m->quit) break;
      const char * name = files[next];
      next = (next + 1) % nfiles;
      snprintf(path, sizeof(path), "%s/%s", MUSIC_DIR, name);
      if(!drmp3_init_file(mp3, path, &music_alloc)) {
        fails++;
        continue;
      }
      if(mp3->channels < 1 || mp3->channels > 2) {
        drmp3_uninit(mp3);
        fails++;
        continue;
      }
      open = 1;
      trackframes = 0;
      music_setname(m, name);
      ndspChnReset(MUSIC_CHANNEL);
      ndspChnSetInterp(MUSIC_CHANNEL, NDSP_INTERP_LINEAR);
      ndspChnSetRate(MUSIC_CHANNEL, mp3->sampleRate);
      ndspChnSetFormat(MUSIC_CHANNEL, mp3->channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
      float mix[12] = { 1.0f, 1.0f };
      ndspChnSetMix(MUSIC_CHANNEL, mix);
      for(int i = 0; i < MUSIC_BUFS; i++) m->wave[i].status = NDSP_WBUF_FREE;
    }
    // Refill every finished buffer
    int filled = 0;
    for(int i = 0; i < MUSIC_BUFS && open && !m->quit; i++) {
      ndspWaveBuf * w = m->wave + i;
      if(w->status != NDSP_WBUF_FREE && w->status != NDSP_WBUF_DONE) continue;
      s16 * buf = m->pcm + i * MUSIC_FRAMES * 2;
      u64 t = svcGetSystemTick();
      drmp3_uint64 got = drmp3_read_pcm_frames_s16(mp3, MUSIC_FRAMES, buf);
      u64 dt = svcGetSystemTick() - t;
      LightLock_Lock(&m->lock);
      m->busy += dt;
      LightLock_Unlock(&m->lock);
      if(got == 0) {
        // End of track (or a decode error): on to the next file
        drmp3_uninit(mp3);
        open = 0;
        if(trackframes == 0) fails++;
        break;
      }
      trackframes += got;
      fails = 0;
      DSP_FlushDataCache(buf, got * mp3->channels * sizeof(s16));
      w->data_pcm16 = buf;
      w->nsamples = got;
      w->looping = false;
      ndspChnWaveBufAdd(MUSIC_CHANNEL, w);
      filled = 1;
    }
    if(open && !filled) LightEvent_Wait(&m->wake);
  }
  if(open) drmp3_uninit(mp3);
  free(mp3);
  for(uint32_t i = 0; i < nfiles; i++) free(files[i]);
  free(files);
  music_setname(m, "");
}

int music_init(Music * m, int ndspok) {
  memset(m, 0, sizeof(Music));
  LightLock_Init(&m->lock);
  LightEvent_Init(&m->wake, RESET_ONESHOT);
  if(!ndspok) return 1;
  m->pcm = linearAlloc(MUSIC_BUFS * MUSIC_FRAMES * 2 * sizeof(s16));
  if(!m->pcm) return 1;
  ndspSetCallback(music_callback, m);
  // Above the main thread (smaller number = higher): decoding comes in short bursts and
  // never needs more than real time, and the music shouldn't skip on a slow frame
  s32 prio = 0x30;
  svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  m->thread = threadCreate(music_thread, m, MUSIC_STACK, prio - 1, -2, false);
  m->running = m->thread != NULL;
  if(!m->running) {
    ndspSetCallback(NULL, NULL);
    linearFree(m->pcm);
    m->pcm = NULL;
    return 1;
  }
  return 0;
}

void music_free(Music * m) {
  if(m->running) {
    m->quit = 1;
    LightEvent_Signal(&m->wake);
    threadJoin(m->thread, U64_MAX);
    threadFree(m->thread);
    ndspSetCallback(NULL, NULL);
    ndspChnWaveBufClear(MUSIC_CHANNEL);
  }
  if(m->pcm) linearFree(m->pcm);
  memset(m, 0, sizeof(Music));
}

void music_name(Music * m, char * out, size_t len) {
  LightLock_Lock(&m->lock);
  snprintf(out, len, "%s", m->name);
  LightLock_Unlock(&m->lock);
}

u64 music_busy(Music * m) {
  LightLock_Lock(&m->lock);
  u64 b = m->busy;
  LightLock_Unlock(&m->lock);
  u64 d = b - m->busyseen;
  m->busyseen = b;
  return d;
}

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

static void music_setvol(float v) {
  float mix[12] = { v, v };
  ndspChnSetMix(MUSIC_CHANNEL, mix);
}

// Drop everything queued on the channel. We own the wave structs, so mark them free.
static void music_clear(Music * m) {
  ndspChnWaveBufClear(MUSIC_CHANNEL);
  for(int i = 0; i < MUSIC_BUFS; i++) m->wave[i].status = NDSP_WBUF_FREE;
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

// Everything the thread owns while playing
typedef struct {
  char ** files;
  uint32_t nfiles;
  drmp3 * mp3;          // Non-NULL = a track is open
  uint32_t fails;       // Files in a row that gave no audio
  u64 trackframes;
} MusicPlay;

static void music_close(MusicPlay * p) {
  if(p->mp3) {
    drmp3_uninit(p->mp3);
    free(p->mp3);
    p->mp3 = NULL;
  }
}

static void music_release(MusicPlay * p) {
  music_close(p);
  for(uint32_t i = 0; i < p->nfiles; i++) free(p->files[i]);
  free(p->files);
  memset(p, 0, sizeof(MusicPlay));
}

// Open the next file that works. Returns 0 if there's nothing playable.
static int music_opennext(Music * m, MusicPlay * p) {
  char path[STORAGE_MAXPATH];
  while(p->fails < p->nfiles) {
    const char * name = p->files[m->next % p->nfiles];
    m->next = (m->next + 1) % p->nfiles;
    snprintf(path, sizeof(path), "%s/%s", MUSIC_DIR, name);
    p->mp3 = malloc(sizeof(drmp3));
    if(!p->mp3) return 0;
    if(!drmp3_init_file(p->mp3, path, &music_alloc)) {
      free(p->mp3);
      p->mp3 = NULL;
      p->fails++;
      continue;
    }
    if(p->mp3->channels < 1 || p->mp3->channels > 2) {
      music_close(p);
      p->fails++;
      continue;
    }
    p->trackframes = 0;
    music_setname(m, name);
    ndspChnReset(MUSIC_CHANNEL);
    ndspChnSetInterp(MUSIC_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(MUSIC_CHANNEL, p->mp3->sampleRate);
    ndspChnSetFormat(MUSIC_CHANNEL, p->mp3->channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    music_setvol(1);
    return 1;
  }
  return 0;
}

// Decode into one free buffer, if there is one. Returns 0 at the end of the track.
static int music_refill(Music * m, MusicPlay * p) {
  for(int i = 0; i < MUSIC_BUFS; i++) {
    ndspWaveBuf * w = m->wave + i;
    if(w->status != NDSP_WBUF_FREE && w->status != NDSP_WBUF_DONE) continue;
    s16 * buf = m->pcm + i * MUSIC_FRAMES * 2;
    u64 t = svcGetSystemTick();
    drmp3_uint64 got = drmp3_read_pcm_frames_s16(p->mp3, MUSIC_FRAMES, buf);
    u64 dt = svcGetSystemTick() - t;
    LightLock_Lock(&m->lock);
    m->busy += dt;
    LightLock_Unlock(&m->lock);
    if(got == 0) {
      if(p->trackframes == 0) p->fails++;
      return 0;
    }
    p->trackframes += got;
    p->fails = 0;
    DSP_FlushDataCache(buf, got * p->mp3->channels * sizeof(s16));
    w->data_pcm16 = buf;
    w->nsamples = got;
    w->looping = false;
    ndspChnWaveBufAdd(MUSIC_CHANNEL, w);
    return 1;
  }
  return 1;
}

static void music_thread(void * arg) {
  Music * m = arg;
  MusicPlay p = { 0 };
  enum { IDLE, PLAYING, FADING } state = IDLE;
  u64 fadestart = 0;
  for(;;) {
    LightEvent_Wait(&m->wake);
    LightLock_Lock(&m->lock);
    u8 quit = m->quit, want = m->want, skip = m->skip;
    m->skip = 0;
    LightLock_Unlock(&m->lock);
    if(quit) break;
    if(want && state != PLAYING) {
      // Start (cutting any fade short)
      music_clear(m);
      p.nfiles = music_list(&p.files);
      p.fails = 0;
      state = PLAYING;
    } else if(!want && state == PLAYING) {
      // Stop: the decoder goes now, what's already decoded plays out fading
      music_release(&p);
      music_setname(m, "");
      state = FADING;
      fadestart = svcGetSystemTick();
    }
    if(state == FADING) {
      float t = (svcGetSystemTick() - fadestart) / (float)SYSCLOCK_ARM11 / MUSIC_FADE_S;
      if(t >= 1 || !music_queued(m)) {
        music_clear(m);
        music_setvol(1);
        state = IDLE;
      } else {
        music_setvol(1 - t);
      }
      continue;
    }
    if(state != PLAYING) continue;
    if(skip && p.mp3) {
      music_clear(m);
      music_close(&p);
    }
    if(!p.mp3) {
      // Let the last track finish before changing rate / format
      if(music_queued(m)) continue;
      if(!music_opennext(m, &p)) {
        // Nothing playable: stay quiet until the next start
        music_release(&p);
        music_setname(m, "");
        state = IDLE;
        continue;
      }
    }
    if(!music_refill(m, &p)) music_close(&p);
  }
  music_release(&p);
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
  // Above the main thread (smaller number = higher): it only ever decodes one small buffer
  // per DSP frame, and the music shouldn't skip on a slow frame
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
    LightLock_Lock(&m->lock);
    m->quit = 1;
    LightLock_Unlock(&m->lock);
    LightEvent_Signal(&m->wake);
    threadJoin(m->thread, U64_MAX);
    threadFree(m->thread);
    ndspSetCallback(NULL, NULL);
    ndspChnWaveBufClear(MUSIC_CHANNEL);
  }
  if(m->pcm) linearFree(m->pcm);
  memset(m, 0, sizeof(Music));
}

static void music_request(Music * m, int want, int skip) {
  LightLock_Lock(&m->lock);
  if(want >= 0) m->want = want;
  if(skip) m->skip = 1;
  LightLock_Unlock(&m->lock);
  LightEvent_Signal(&m->wake);
}

void music_play(Music * m) {
  music_request(m, 1, 0);
}

void music_stop(Music * m) {
  music_request(m, 0, 0);
}

void music_skip(Music * m) {
  music_request(m, -1, 1);
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

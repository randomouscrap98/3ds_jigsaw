#include "music.h"
#include "musicdec.h"
#include "rng.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int music_cmp(const void * a, const void * b) {
  return strcasecmp(*(char * const *)a, *(char * const *)b);
}

// Sorted playable file names in MUSIC_DIR. Returns count; *out = NULL if none.
static uint32_t music_list(char *** out) {
  *out = NULL;
  DIR * d = opendir(MUSIC_DIR);
  if(!d) return 0;
  char ** names = NULL;
  uint32_t n = 0, cap = 0;
  struct dirent * de;
  while((de = readdir(d))) {
    if(musicdec_kind(de->d_name) == MUSICDEC_NONE) continue;
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
  MusicDec dec;          // kind != MUSICDEC_NONE = a track is open
  uint32_t fails;       // Files in a row that gave no audio
  u64 trackframes;
  uint32_t * perm;      // Shuffled play order (NULL = couldn't allocate: play in order)
  uint32_t spos;        // Next position in perm
  uint32_t last;        // Index of the file played last
  Rng rng;
} MusicPlay;

// New random order, not starting with the file just played
static void music_shuffle(MusicPlay * p) {
  for(uint32_t i = 0; i < p->nfiles; i++) p->perm[i] = i;
  for(uint32_t i = p->nfiles - 1; i > 0; i--) {
    uint32_t j = rng_below(&p->rng, i + 1);
    uint32_t t = p->perm[i]; p->perm[i] = p->perm[j]; p->perm[j] = t;
  }
  if(p->nfiles > 1 && p->perm[0] == p->last) {
    p->perm[0] = p->perm[1];
    p->perm[1] = p->last;
  }
  p->spos = 0;
}

// Which file to play next. In order continues after whatever played last, shuffled or not.
static uint32_t music_pick(Music * m, MusicPlay * p, int shuffle) {
  uint32_t i;
  if(shuffle && p->perm) {
    if(p->spos >= p->nfiles) music_shuffle(p);
    i = p->perm[p->spos++];
  } else {
    i = m->next % p->nfiles;
  }
  m->next = (i + 1) % p->nfiles;
  p->last = i;
  return i;
}

static void music_close(MusicPlay * p) {
  musicdec_close(&p->dec);
}

static void music_release(MusicPlay * p) {
  music_close(p);
  for(uint32_t i = 0; i < p->nfiles; i++) free(p->files[i]);
  free(p->files);
  free(p->perm);
  memset(p, 0, sizeof(MusicPlay));
}

// Open the next file that works. Returns 0 if there's nothing playable.
// vol: channel gain (ndspChnReset puts the mix back to 1)
static int music_opennext(Music * m, MusicPlay * p, int shuffle, float vol) {
  char path[STORAGE_MAXPATH];
  while(p->fails < p->nfiles) {
    const char * name = p->files[music_pick(m, p, shuffle)];
    snprintf(path, sizeof(path), "%s/%s", MUSIC_DIR, name);
    if(musicdec_open(&p->dec, path, musicdec_kind(name), svcGetSystemTick)) {
      p->fails++;
      continue;
    }
    p->trackframes = 0;
    music_setname(m, name);
    ndspChnReset(MUSIC_CHANNEL);
    ndspChnSetInterp(MUSIC_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(MUSIC_CHANNEL, p->dec.rate);
    ndspChnSetFormat(MUSIC_CHANNEL, p->dec.channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    music_setvol(vol);
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
    u64 t = svcGetSystemTick(), r = p->dec.readtime;
    uint32_t got = musicdec_read(&p->dec, buf, MUSIC_FRAMES);
    u64 dt = svcGetSystemTick() - t, dr = p->dec.readtime - r;
    LightLock_Lock(&m->lock);
    m->busy += dt - dr;
    m->sdbusy += dr;
    LightLock_Unlock(&m->lock);
    if(got == 0) {
      if(p->trackframes == 0) p->fails++;
      return 0;
    }
    p->trackframes += got;
    p->fails = 0;
    DSP_FlushDataCache(buf, got * p->dec.channels * sizeof(s16));
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
  float applied = -1;  // Gain last given to the channel (-1 = fading, set it again)
  for(;;) {
    LightEvent_Wait(&m->wake);
    LightLock_Lock(&m->lock);
    u8 quit = m->quit, want = m->mode != MUSIC_OFF, shuffle = m->mode == MUSIC_SHUFFLE, skip = m->skip;
    float vol = m->volume;
    m->skip = 0;
    LightLock_Unlock(&m->lock);
    if(quit) break;
    if(want && state != PLAYING) {
      // Start (cutting any fade short)
      music_clear(m);
      p.nfiles = music_list(&p.files);
      p.fails = 0;
      p.last = UINT32_MAX;
      p.perm = p.nfiles ? malloc(sizeof(uint32_t) * p.nfiles) : NULL;
      if(p.perm) {
        rng_init(&p.rng, (uint32_t)svcGetSystemTick());
        music_shuffle(&p);
      }
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
        state = IDLE;
      } else {
        music_setvol(vol * (1 - t));
        applied = -1;
      }
      continue;
    }
    if(vol != applied) {
      music_setvol(vol);
      applied = vol;
    }
    if(state != PLAYING) continue;
    if(skip && p.dec.kind) {
      music_clear(m);
      music_close(&p);
    }
    if(!p.dec.kind) {
      // Let the last track finish before changing rate / format
      if(music_queued(m)) continue;
      if(!music_opennext(m, &p, shuffle, vol)) {
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
  m->volume = 1;
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

static void music_request(Music * m, int mode, int skip) {
  LightLock_Lock(&m->lock);
  if(mode >= 0) m->mode = mode;
  if(skip) m->skip = 1;
  LightLock_Unlock(&m->lock);
  LightEvent_Signal(&m->wake);
}

void music_setmode(Music * m, int mode) {
  music_request(m, mode, 0);
}

void music_setvolume(Music * m, float volume) {
  LightLock_Lock(&m->lock);
  m->volume = volume;
  LightLock_Unlock(&m->lock);
  LightEvent_Signal(&m->wake);
}

void music_skip(Music * m) {
  music_request(m, -1, 1);
}

void music_name(Music * m, char * out, size_t len) {
  LightLock_Lock(&m->lock);
  snprintf(out, len, "%s", m->name);
  LightLock_Unlock(&m->lock);
}

void music_busy(Music * m, u64 * decode, u64 * sd) {
  LightLock_Lock(&m->lock);
  u64 b = m->busy, s = m->sdbusy;
  LightLock_Unlock(&m->lock);
  *decode = b - m->busyseen;
  *sd = s - m->sdseen;
  m->busyseen = b;
  m->sdseen = s;
}

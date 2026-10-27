#include "musicdec.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void * musicdec_malloc(size_t sz, void * u) {
  return sz > MUSICDEC_MAXALLOC ? NULL : malloc(sz);
}

static void * musicdec_realloc(void * p, size_t sz, void * u) {
  return sz > MUSICDEC_MAXALLOC ? NULL : realloc(p, sz);
}

static void musicdec_mfree(void * p, void * u) {
  free(p);
}

// The one dr_mp3 / dr_flac implementation in the app. stb_vorbis.c is compiled on its own.
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

static const drmp3_allocation_callbacks musicdec_mp3alloc = {
  .onMalloc = musicdec_malloc, .onRealloc = musicdec_realloc, .onFree = musicdec_mfree,
};
static const drflac_allocation_callbacks musicdec_flacalloc = {
  .onMalloc = musicdec_malloc, .onRealloc = musicdec_realloc, .onFree = musicdec_mfree,
};

#define MP3(d) ((drmp3 *)(d)->mp3)
#define FLAC(d) ((drflac *)(d)->flac)

MusicKind musicdec_kind(const char * name) {
  const char * dot = strrchr(name, '.');
  if(!dot) return MUSICDEC_NONE;
  if(!strcasecmp(dot, ".mp3")) return MUSICDEC_MP3;
  if(!strcasecmp(dot, ".flac")) return MUSICDEC_FLAC;
  if(!strcasecmp(dot, ".ogg")) return MUSICDEC_OGG;
  return MUSICDEC_NONE;
}

// File callbacks for dr_mp3 / dr_flac (their seek origins are SET, CUR, END like stdio's)
static size_t musicdec_fread(void * u, void * out, size_t n) {
  MusicDec * d = u;
  uint64_t t = d->now ? d->now() : 0;
  size_t got = fread(out, 1, n, d->file);
  if(d->now) d->readtime += d->now() - t;
  return got;
}

static int musicdec_fseek(void * u, int offset, int origin) {
  static const int whence[] = { SEEK_SET, SEEK_CUR, SEEK_END };
  return origin >= 0 && origin <= 2 && fseek(((MusicDec *)u)->file, offset, whence[origin]) == 0;
}

static int musicdec_ftell(void * u, int64_t * pos) {
  long p = ftell(((MusicDec *)u)->file);
  if(p < 0) return 0;
  *pos = p;
  return 1;
}

static drmp3_bool32 musicdec_mp3seek(void * u, int offset, drmp3_seek_origin origin) {
  return musicdec_fseek(u, offset, (int)origin);
}

static drmp3_bool32 musicdec_mp3tell(void * u, drmp3_int64 * pos) {
  int64_t p;
  if(!musicdec_ftell(u, &p)) return DRMP3_FALSE;
  *pos = p;
  return DRMP3_TRUE;
}

static drflac_bool32 musicdec_flacseek(void * u, int offset, drflac_seek_origin origin) {
  return musicdec_fseek(u, offset, (int)origin);
}

static drflac_bool32 musicdec_flactell(void * u, drflac_int64 * pos) {
  int64_t p;
  if(!musicdec_ftell(u, &p)) return DRFLAC_FALSE;
  *pos = p;
  return DRFLAC_TRUE;
}

void musicdec_close(MusicDec * d) {
  if(d->mp3) {
    drmp3_uninit(MP3(d));
    free(d->mp3);
  }
  if(d->flac) drflac_close(FLAC(d));
  if(d->ogg) stb_vorbis_close(d->ogg);
  if(d->file) fclose(d->file);
  free(d->filebuf);
  memset(d, 0, sizeof(MusicDec));
}

int musicdec_open(MusicDec * d, const char * path, MusicKind kind, uint64_t (*now)(void)) {
  memset(d, 0, sizeof(MusicDec));
  if(kind == MUSICDEC_NONE) return 1;
  d->now = now;
  d->file = fopen(path, "rb");
  if(!d->file) return 1;
  d->filebuf = malloc(MUSICDEC_FILEBUF);
  if(d->filebuf) setvbuf(d->file, d->filebuf, _IOFBF, MUSICDEC_FILEBUF);
  uint32_t srcch = 0;
  if(kind == MUSICDEC_MP3) {
    d->mp3 = malloc(sizeof(drmp3));
    if(!d->mp3 || !drmp3_init(MP3(d), musicdec_fread, musicdec_mp3seek, musicdec_mp3tell, NULL, d,
        &musicdec_mp3alloc)) {
      free(d->mp3);
      d->mp3 = NULL;
      musicdec_close(d);
      return 1;
    }
    srcch = MP3(d)->channels;
    d->rate = MP3(d)->sampleRate;
  } else if(kind == MUSICDEC_FLAC) {
    d->flac = drflac_open(musicdec_fread, musicdec_flacseek, musicdec_flactell, d, &musicdec_flacalloc);
    if(!d->flac) {
      musicdec_close(d);
      return 1;
    }
    srcch = FLAC(d)->channels;
    d->rate = FLAC(d)->sampleRate;
  } else {
    int err = 0;
    d->ogg = stb_vorbis_open_file(d->file, 0, &err, NULL);
    if(!d->ogg) {
      musicdec_close(d);
      return 1;
    }
    stb_vorbis_info info = stb_vorbis_get_info(d->ogg);
    // stb_vorbis mixes anything over 2 channels down to the 2 we ask for
    srcch = info.channels > 2 ? 2 : info.channels;
    d->rate = info.sample_rate;
  }
  d->kind = kind;
  d->channels = srcch;
  // More than stereo only from dr_mp3 / dr_flac, which don't mix down: skip those
  if(d->channels < 1 || d->channels > 2 || d->rate == 0) {
    musicdec_close(d);
    return 1;
  }
  return 0;
}

uint32_t musicdec_read(MusicDec * d, int16_t * out, uint32_t frames) {
  switch(d->kind) {
    case MUSICDEC_MP3: return (uint32_t)drmp3_read_pcm_frames_s16(MP3(d), frames, out);
    case MUSICDEC_FLAC: return (uint32_t)drflac_read_pcm_frames_s16(FLAC(d), frames, out);
    case MUSICDEC_OGG: {
      int n = stb_vorbis_get_samples_short_interleaved(d->ogg, d->channels, out, frames * d->channels);
      return n > 0 ? (uint32_t)n : 0;
    }
    default: return 0;
  }
}


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

void musicdec_close(MusicDec * d) {
  if(d->mp3) {
    drmp3_uninit(MP3(d));
    free(d->mp3);
  }
  if(d->flac) drflac_close(FLAC(d));
  if(d->ogg) stb_vorbis_close(d->ogg);
  if(d->oggfile) fclose(d->oggfile);
  free(d->oggbuf);
  memset(d, 0, sizeof(MusicDec));
}

int musicdec_open(MusicDec * d, const char * path, MusicKind kind) {
  memset(d, 0, sizeof(MusicDec));
  uint32_t srcch = 0;
  if(kind == MUSICDEC_MP3) {
    d->mp3 = malloc(sizeof(drmp3));
    if(!d->mp3) return 1;
    if(!drmp3_init_file(MP3(d), path, &musicdec_mp3alloc)) {
      free(d->mp3);
      d->mp3 = NULL;
      return 1;
    }
    srcch = MP3(d)->channels;
    d->rate = MP3(d)->sampleRate;
  } else if(kind == MUSICDEC_FLAC) {
    d->flac = drflac_open_file(path, &musicdec_flacalloc);
    if(!d->flac) return 1;
    srcch = FLAC(d)->channels;
    d->rate = FLAC(d)->sampleRate;
  } else if(kind == MUSICDEC_OGG) {
    d->oggfile = fopen(path, "rb");
    if(!d->oggfile) return 1;
    d->oggbuf = malloc(MUSICDEC_OGGBUF);
    if(d->oggbuf) setvbuf(d->oggfile, d->oggbuf, _IOFBF, MUSICDEC_OGGBUF);
    int err = 0;
    d->ogg = stb_vorbis_open_file(d->oggfile, 0, &err, NULL);
    if(!d->ogg) {
      musicdec_close(d);
      return 1;
    }
    stb_vorbis_info info = stb_vorbis_get_info(d->ogg);
    // stb_vorbis mixes anything over 2 channels down to the 2 we ask for
    srcch = info.channels > 2 ? 2 : info.channels;
    d->rate = info.sample_rate;
  } else {
    return 1;
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


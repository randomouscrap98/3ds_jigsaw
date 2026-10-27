#include "test.h"
#include "musicdec.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TEST_MUSIC_SECONDS 10

void test_musicdec(void) {
  CHECK(musicdec_kind("a.MP3") == MUSICDEC_MP3);
  CHECK(musicdec_kind("b.flac") == MUSICDEC_FLAC);
  CHECK(musicdec_kind("c.Ogg") == MUSICDEC_OGG);
  CHECK(musicdec_kind("d.wav") == MUSICDEC_NONE);
  CHECK(musicdec_kind("noext") == MUSICDEC_NONE);
  MusicDec d;
  CHECK(musicdec_open(&d, "/nonexistent/x.mp3", MUSICDEC_MP3, NULL) != 0);
  CHECK(d.kind == MUSICDEC_NONE);
}

static uint64_t test_clock(void) {
  return (uint64_t)clock();
}

// Decode the files from tools/make_music_tests.sh; also prints decode speed
void test_musicdec_files(const char * dir) {
  static const struct { const char * name; uint32_t channels, rate; int ok; } files[] = {
    { "stereo44.mp3", 2, 44100, 1 }, { "mono48.mp3", 1, 48000, 1 },
    { "stereo44.flac", 2, 44100, 1 }, { "mono48.flac", 1, 48000, 1 },
    { "stereo44.ogg", 2, 44100, 1 }, { "mono48.ogg", 1, 48000, 1 },
    { "six.flac", 0, 0, 0 }, { "six.ogg", 2, 48000, 1 },
    { "junk.mp3", 0, 0, 0 }, { "junk.flac", 0, 0, 0 }, { "junk.ogg", 0, 0, 0 },
  };
  int16_t * buf = malloc(sizeof(int16_t) * 2048 * 2);
  char path[1024];
  for(size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    snprintf(path, sizeof(path), "%s/%s", dir, files[i].name);
    MusicDec d;
    int r = musicdec_open(&d, path, musicdec_kind(files[i].name), test_clock);
    if(!files[i].ok) {
      // Junk may "open" (MP3 sync words turn up in random data) but must give ~no audio
      uint64_t frames = 0;
      if(r == 0) {
        uint32_t n;
        while((n = musicdec_read(&d, buf, 2048))) frames += n;
        musicdec_close(&d);
      }
      printf("  %-14s %s, %llu frames\n", files[i].name, r ? "rejected" : "opened",
          (unsigned long long)frames);
      CHECK(r != 0 || frames < 44100);
      continue;
    }
    CHECK(r == 0);
    if(r) continue;
    CHECK(d.channels == files[i].channels);
    CHECK(d.rate == files[i].rate);
    uint64_t frames = 0;
    int peak = 0;
    clock_t t = clock();
    uint32_t n;
    while((n = musicdec_read(&d, buf, 2048))) {
      for(uint32_t k = 0; k < n * d.channels; k++) if(abs(buf[k]) > peak) peak = abs(buf[k]);
      frames += n;
    }
    double s = (double)(clock() - t) / CLOCKS_PER_SEC;
    double secs = (double)frames / d.rate;
    printf("  %-14s %lu ch %lu Hz, %.2f s, peak %d, decoded at %.0fx real time, reads %.1f%% (host)\n",
        files[i].name, (unsigned long)d.channels, (unsigned long)d.rate, secs, peak,
        s > 0 ? secs / s : 0.0, s > 0 ? 100.0 * d.readtime / CLOCKS_PER_SEC / s : 0.0);
    CHECK(secs > TEST_MUSIC_SECONDS - 0.1 && secs < TEST_MUSIC_SECONDS + 0.1);
    CHECK(peak > 1000);  // ffmpeg's sine is 1/8 scale, less after stereo mixing
    musicdec_close(&d);
  }
  free(buf);
}

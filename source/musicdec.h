#ifndef __HEADER_JIGSAW_MUSICDEC__
#define __HEADER_JIGSAW_MUSICDEC__

#include <stdint.h>
#include <stdio.h>

#define MUSICDEC_MAXALLOC (1024 * 1024)  // Biggest dr_mp3 / dr_flac allocation allowed (a
                                         // non-MP3 file would otherwise be read in whole
                                         // looking for a frame; big-block FLACs need a few
                                         // hundred KB)
#define MUSICDEC_FILEBUF  (64 * 1024)    // stdio buffer: few, big SD reads (the decoders ask
                                         // for 4 KB or less, stb_vorbis a byte at a time)

// Streaming audio decoding for MP3 (dr_mp3), FLAC (dr_flac) and Ogg Vorbis (stb_vorbis),
// to 16-bit interleaved mono or stereo. Picked by file extension.
typedef enum { MUSICDEC_NONE, MUSICDEC_MP3, MUSICDEC_FLAC, MUSICDEC_OGG } MusicKind;

// One open track, whatever the format. Output is always 16-bit, 1 or 2 channels.
typedef struct {
  MusicKind kind;       // MUSICDEC_NONE = closed
  void * mp3;           // drmp3 (anonymous struct in dr_mp3.h, hence void)
  void * flac;          // drflac
  struct stb_vorbis * ogg;
  FILE * file;
  char * filebuf;       // MUSICDEC_FILEBUF stdio buffer for file
  uint64_t (*now)(void);  // Clock for timing reads (NULL = don't)
  uint64_t readtime;    // Time spent in file reads, in now() units (MP3 / FLAC only;
                        // stb_vorbis reads the FILE itself)
  uint32_t channels;    // Output channels
  uint32_t rate;
} MusicDec;

// Format from the file name's extension (MUSICDEC_NONE = not a music file)
MusicKind musicdec_kind(const char * name);
// 0 = opened. Fails for unreadable / invalid files and anything over 2 channels that the
// decoder can't mix down (MP3 and FLAC; Ogg is mixed down to stereo). now = clock used to
// time file reads into readtime, or NULL.
int musicdec_open(MusicDec * d, const char * path, MusicKind kind, uint64_t (*now)(void));
void musicdec_close(MusicDec * d);
// Interleaved frames into out (room for frames * channels). Returns frames; 0 = end/error.
uint32_t musicdec_read(MusicDec * d, int16_t * out, uint32_t frames);

#endif

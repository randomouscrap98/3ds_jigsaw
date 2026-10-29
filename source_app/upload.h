#ifndef __HEADER_JIGSAW_UPLOAD__
#define __HEADER_JIGSAW_UPLOAD__

#include <3ds.h>

#include "qrcodegen.h"
#include "textdraw.h"
#include "webup.h"

// The "Add pictures and music over Wi-Fi" screen: starts the network and the upload server,
// shows the address (and a QR code for it) and what arrived. Pictures land in the puzzles
// folder, music (.mp3 / .flac / .ogg, checked by decoding a little) in the music folder.

#define UPLOAD_PORT 80
#define UPLOAD_PAGE "romfs:/upload.html"
#define UPLOAD_LINES 6                    // Last events shown
#define UPLOAD_RETRY_MS 2000              // Try again this often while the network isn't up
#define UPLOAD_SOCBUF 0x100000            // Network service buffer (socInit)
#define UPLOAD_STACK (64 * 1024)          // Server thread (webup reads in 16 KB stack chunks)
#define UPLOAD_IDLE_NS (10 * 1000 * 1000)   // Server thread sleep: no client
#define UPLOAD_CLIENT_NS (1000 * 1000)      // Server thread sleep: client connected (drain fast)

typedef struct {
  u32 * socbuf;
  int socok;              // socInit done
  int running;            // Server listening
  int paused;             // Stopped for sleep/HOME
  u64 lastretry;
  char * page;
  size_t pagelen;
  WebUp web;
  // The server runs on its own thread so the socket is drained as soon as data comes (once
  // a frame was too slow: the 3DS dropped packets and uploads stalled). lock covers web,
  // lines, added and busy.
  Thread thread;
  LightLock lock;
  volatile int quit;
  char url[48];
  uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
  int qrok;
  char problem[WEBUP_MSG];  // Why it isn't running
  char lines[UPLOAD_LINES][WEBUP_MSG + 16];
  u8 linegood[UPLOAD_LINES];
  int nlines;
  uint32_t added;         // Pictures saved this visit
  int busy;               // Receiving right now
} Upload;

// Open the screen (it always opens; problem says what's missing)
void upload_open(Upload * u);
void upload_close(Upload * u);
// Sleep/HOME: networking stops; it starts again on resume
void upload_pause(Upload * u);
void upload_resume(Upload * u);
// Returns 1 when the player is done (B or START)
int upload_update(Upload * u, u32 down);
void upload_render_top(Upload * u, TextDraw * td);
void upload_render_bottom(Upload * u, TextDraw * td);

#endif

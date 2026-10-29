#ifndef __HEADER_JIGSAW_WEBUP__
#define __HEADER_JIGSAW_WEBUP__

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "chunkwriter.h"

// A tiny web server for adding files: GET / gives the upload page, POST <route>?name=...
// saves the body into the route's folder under a free, safe name (routes: pictures,
// music). The body streams to the SD through a ChunkWriter (writes on its own thread, so
// receiving never waits on it). One client at a time, never blocks (call webup_poll every
// frame), drops a client that goes quiet. Plain BSD sockets: on the 3DS the caller sets up
// the network (socInit) first; on a PC it just works (host tests). HTTP parsing is
// picohttpparser.

#define WEBUP_MAXHEAD 8192          // Request line + headers
#define WEBUP_MAXBODY (500LL << 20) // Biggest upload (sanity cap)
#define WEBUP_TIMEOUT_MS 15000      // A client quiet this long is dropped
#define WEBUP_LINGER_MS 2000        // After a response: wait this long for the client to hang up
#define WEBUP_CHUNK 16384           // Read/write size
#define WEBUP_FRAMEBYTES (256 << 10)  // Most bytes moved per webup_poll (keeps frames smooth)
#define WEBUP_MSG 160

// What happened (webup_poll result flags); message has the details
#define WEBUP_EV_SAVED 1      // A file was saved (message: its file name, route: which kind)
#define WEBUP_EV_ERROR 2      // An upload failed (message: why)
#define WEBUP_EV_BUSY 4       // Receiving (received / expected bytes)

// 1 if the file at path (a temp file) is one the app can use; name = the name it will get
typedef int (*webup_check_fn)(const char * path, const char * name);

// What one POST path takes
typedef struct {
  const char * path;        // "/upload"
  const char * dir;         // Saved here
  const char * ext;         // Always saved with this extension, or NULL: keep the sent
                            // name's (lowercased), which must be one of keep
  const char * const * keep;  // NULL-terminated, e.g. { ".mp3", ".flac", NULL }
  webup_check_fn check;     // NULL = anything goes
  const char * bad;         // Error text when check refuses
} WebUpRoute;

typedef struct {
  int lsock, csock;
  int state;
  uint64_t last;            // Last activity on the client (ms)
  char head[WEBUP_MAXHEAD + 1];
  size_t headlen;
  // Upload being received
  ChunkWriter cw;           // Body -> <name>.tmp (open while writing is set)
  int writing;
  int sdwait;               // Receiving is held up: both buffers still being written
  char name[128];           // Final file name
  const WebUpRoute * route; // Route of the upload being received / last saved
  size_t expect, received;
  // Response being sent
  char out[512];
  size_t outlen, outsent;
  const char * extra;       // Then this (the page), extralen bytes
  size_t extralen;
  // Setup
  const char * page;
  size_t pagelen;
  const WebUpRoute * routes;
  int nroutes;
  char message[WEBUP_MSG];
} WebUp;

// Listen on port (all interfaces). page is served at /; page and routes are kept by the
// caller. Returns 0, or 1 with message set.
int webup_start(WebUp * w, int port, const char * page, size_t pagelen, const WebUpRoute * routes, int nroutes);
void webup_stop(WebUp * w);
// Do whatever the sockets allow right now. now = a millisecond clock. Returns WEBUP_EV_*.
int webup_poll(WebUp * w, uint64_t now);

// Helpers (exposed for tests): percent-decode src into out (always terminated). Returns
// the length. Query value of key in a path ("/x?a=1&b=2"), decoded; 0 if found.
size_t webup_urldecode(const char * src, size_t len, char * out, size_t outlen);
int webup_query(const char * path, size_t pathlen, const char * key, char * out, size_t outlen);

#endif

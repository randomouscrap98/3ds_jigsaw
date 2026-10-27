#ifndef __HEADER_JIGSAW_WEBUP__
#define __HEADER_JIGSAW_WEBUP__

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// A tiny web server for adding pictures: GET / gives the upload page, POST /upload?name=...
// with an image body saves it into a folder under a free, safe name. One client at a time,
// never blocks (call webup_poll every frame), drops a client that goes quiet. Plain BSD
// sockets: on the 3DS the caller sets up the network (socInit) first; on a PC it just works
// (host tests). HTTP parsing is picohttpparser.

#define WEBUP_MAXHEAD 8192          // Request line + headers
#define WEBUP_MAXBODY (8 << 20)     // Biggest upload (the page sends ~1024 px JPEGs: < 1 MB)
#define WEBUP_TIMEOUT_MS 15000      // A client quiet this long is dropped
#define WEBUP_LINGER_MS 2000        // After a response: wait this long for the client to hang up
#define WEBUP_CHUNK 16384           // Read/write size
#define WEBUP_FRAMEBYTES (256 << 10)  // Most bytes moved per webup_poll (keeps frames smooth)
#define WEBUP_MSG 160

// What happened (webup_poll result flags); message has the details
#define WEBUP_EV_SAVED 1      // A picture was saved (message: its file name)
#define WEBUP_EV_ERROR 2      // An upload failed (message: why)
#define WEBUP_EV_BUSY 4       // Receiving (received / expected bytes)

// 1 if the file at path is a picture the app can use
typedef int (*webup_check_fn)(const char * path);

typedef struct {
  int lsock, csock;
  int state;
  uint64_t last;            // Last activity on the client (ms)
  char head[WEBUP_MAXHEAD + 1];
  size_t headlen;
  // Upload being received
  FILE * body;
  char name[128];           // Final file name
  size_t expect, received;
  // Response being sent
  char out[512];
  size_t outlen, outsent;
  const char * extra;       // Then this (the page), extralen bytes
  size_t extralen;
  // Setup
  const char * page;
  size_t pagelen;
  char dir[256];
  webup_check_fn check;
  char message[WEBUP_MSG];
} WebUp;

// Listen on port (all interfaces). page is served at / (kept by the caller). Returns 0, or
// 1 with message set.
int webup_start(WebUp * w, int port, const char * page, size_t pagelen, const char * dir, webup_check_fn check);
void webup_stop(WebUp * w);
// Do whatever the sockets allow right now. now = a millisecond clock. Returns WEBUP_EV_*.
int webup_poll(WebUp * w, uint64_t now);

// Helpers (exposed for tests): percent-decode src into out (always terminated). Returns
// the length. Query value of key in a path ("/x?a=1&b=2"), decoded; 0 if found.
size_t webup_urldecode(const char * src, size_t len, char * out, size_t outlen);
int webup_query(const char * path, size_t pathlen, const char * key, char * out, size_t outlen);

#endif

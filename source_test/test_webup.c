#include "test.h"
#include "webup.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// The upload server end to end over localhost: requests go out from a non-blocking client
// socket while webup_poll runs, like frames on the 3DS

// File starts with these bytes
static int webup_starts(const char * path, const char * magic, size_t len) {
  FILE * f = fopen(path, "rb");
  if(!f) return 0;
  char b[8] = {0};
  size_t n = fread(b, 1, len, f);
  fclose(f);
  return n == len && !memcmp(b, magic, len);
}

static int webup_isjpeg(const char * path, const char * name) {
  return webup_starts(path, "\xFF\xD8", 2);
}

// Stand-in music check: "ID3" at the start, and the final name (not the .tmp) is .mp3/.ogg
static int webup_ismusic(const char * path, const char * name) {
  const char * dot = strrchr(name, '.');
  return dot && (!strcmp(dot, ".mp3") || !strcmp(dot, ".ogg")) && webup_starts(path, "ID3", 3);
}

// Send req (len bytes), run the server until it closes the connection, collect the reply.
// clock advances 1 ms per round. Returns events seen.
static int http(WebUp * w, int port, const char * req, size_t len, char * reply, size_t replymax, uint64_t * clock) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(connect(s, (struct sockaddr *)&a, sizeof(a))) { close(s); return -1; }
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
  size_t sent = 0, got = 0;
  int ev = 0;
  for(int round = 0; round < 200000; round++) {
    if(sent < len) {
      ssize_t n = send(s, req + sent, len - sent, MSG_NOSIGNAL);
      if(n > 0) sent += n;
    }
    ev |= webup_poll(w, ++*clock);
    ssize_t n = recv(s, reply + got, replymax - 1 - got, 0);
    if(n > 0) got += n;
    else if(n == 0) break;
    else if(errno != EAGAIN && errno != EWOULDBLOCK) break;
  }
  reply[got] = 0;
  close(s);
  return ev;
}

static int exists(const char * dir, const char * name) {
  char p[512];
  snprintf(p, sizeof(p), "%s/%s", dir, name);
  struct stat st;
  return stat(p, &st) == 0 ? (int)st.st_size : -1;
}

void test_webup(void) {
  // Helpers
  char out[64];
  CHECK(webup_urldecode("Cat%20photo+1%2", 15, out, sizeof(out)) == 13 && !strcmp(out, "Cat photo 1%2"));
  CHECK(webup_query("/upload?x=1&name=a%2Fb.jpg&y", 27, "name", out, sizeof(out)) == 0 && !strcmp(out, "a/b.jpg"));
  CHECK(webup_query("/upload?names=1", 15, "name", out, sizeof(out)) == 1);
  CHECK(webup_query("/upload", 7, "name", out, sizeof(out)) == 1);

  char dir[] = "/tmp/jigsaw_webup_XXXXXX", mdir[] = "/tmp/jigsaw_webupm_XXXXXX";
  CHECK(mkdtemp(dir) != NULL && mkdtemp(mdir) != NULL);
  static const char * const musicexts[] = { ".mp3", ".ogg", NULL };
  const WebUpRoute routes[] = {
    { "/upload", dir, ".jpg", NULL, webup_isjpeg, "Not a picture" },
    { "/music", mdir, NULL, musicexts, webup_ismusic, "Not music" },
  };
  const char page[] = "<html>upload page</html>";
  WebUp w;
  int port = 0;
  for(int p = 18000 + getpid() % 2000, tries = 0; tries < 20 && !port; p++, tries++) {
    if(!webup_start(&w, p, page, strlen(page), routes, 2)) port = p;
  }
  CHECK(port != 0);
  if(!port) return;
  uint64_t clock = 0;
  static char reply[1 << 16];

  // The page
  const char * get = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
  http(&w, port, get, strlen(get), reply, sizeof(reply), &clock);
  CHECK(!strncmp(reply, "HTTP/1.1 200", 12) && strstr(reply, "Content-Length: 24") && strstr(reply, "\r\n\r\n<html>upload page</html>"));
  const char * nf = "GET /favicon.ico HTTP/1.1\r\n\r\n";
  http(&w, port, nf, strlen(nf), reply, sizeof(reply), &clock);
  CHECK(!strncmp(reply, "HTTP/1.1 404", 12));

  // An upload (bigger than one read), then the same name again
  size_t blen = 300000;
  char * req = malloc(blen + 256);
  for(int k = 0; k < 2; k++) {
    int h = sprintf(req, "POST /upload?name=Cat%%20photo.png HTTP/1.1\r\nContent-Length: %lu\r\n\r\n", (unsigned long)blen);
    memset(req + h, 'x', blen);
    req[h] = (char)0xFF; req[h + 1] = (char)0xD8;
    int ev = http(&w, port, req, h + blen, reply, sizeof(reply), &clock);
    CHECK((ev & WEBUP_EV_SAVED) && !strncmp(reply, "HTTP/1.1 200", 12));
    CHECK(strstr(reply, k ? "Saved as Cat photo (2).jpg" : "Saved as Cat photo.jpg") != NULL);
  }
  CHECK(exists(dir, "Cat photo.jpg") == (int)blen && exists(dir, "Cat photo (2).jpg") == (int)blen);

  // Not a picture: refused, nothing left behind
  int h = sprintf(req, "POST /upload?name=junk.jpg HTTP/1.1\r\nContent-Length: 100\r\n\r\n");
  memset(req + h, 'z', 100);
  int ev = http(&w, port, req, h + 100, reply, sizeof(reply), &clock);
  CHECK((ev & WEBUP_EV_ERROR) && !strncmp(reply, "HTTP/1.1 400", 12));
  CHECK(exists(dir, "junk.jpg") < 0 && exists(dir, "junk.jpg.tmp") < 0);
  // No length, too big
  const char * nolen = "POST /upload?name=a HTTP/1.1\r\n\r\n";
  http(&w, port, nolen, strlen(nolen), reply, sizeof(reply), &clock);
  CHECK(!strncmp(reply, "HTTP/1.1 411", 12));
  const char * big = "POST /upload?name=a HTTP/1.1\r\nContent-Length: 99999999999\r\n\r\n";
  http(&w, port, big, strlen(big), reply, sizeof(reply), &clock);
  CHECK(!strncmp(reply, "HTTP/1.1 413", 12));
  const char * garbage = "\x01\x02 nonsense\r\n\r\n";
  http(&w, port, garbage, strlen(garbage), reply, sizeof(reply), &clock);
  CHECK(!strncmp(reply, "HTTP/1.1 400", 12));

  // Music keeps its (lowercased) extension, goes to its own folder, the check sees the final
  // name; a picture with the same base name doesn't clash
  const char * song = "POST /music?name=Cat%20photo.MP3 HTTP/1.1\r\nContent-Length: 10\r\n\r\nID3.......";
  ev = http(&w, port, song, strlen(song), reply, sizeof(reply), &clock);
  CHECK((ev & WEBUP_EV_SAVED) && w.route == routes + 1 && strstr(reply, "Saved as Cat photo.mp3") != NULL);
  CHECK(exists(mdir, "Cat photo.mp3") == 10 && exists(dir, "Cat photo.mp3") < 0);
  ev = http(&w, port, song, strlen(song), reply, sizeof(reply), &clock);
  CHECK(strstr(reply, "Saved as Cat photo (2).mp3") != NULL);
  // Wrong or missing extension: refused before the body; check refuses: its text, no .tmp
  const char * wav = "POST /music?name=a.wav HTTP/1.1\r\nContent-Length: 10\r\n\r\nID3.......";
  ev = http(&w, port, wav, strlen(wav), reply, sizeof(reply), &clock);
  CHECK((ev & WEBUP_EV_ERROR) && !strncmp(reply, "HTTP/1.1 415", 12) && exists(mdir, "a.wav") < 0);
  const char * noext = "POST /music?name=song HTTP/1.1\r\nContent-Length: 10\r\n\r\nID3.......";
  http(&w, port, noext, strlen(noext), reply, sizeof(reply), &clock);
  CHECK(!strncmp(reply, "HTTP/1.1 415", 12));
  const char * notsong = "POST /music?name=b.ogg HTTP/1.1\r\nContent-Length: 10\r\n\r\nOggS......";
  ev = http(&w, port, notsong, strlen(notsong), reply, sizeof(reply), &clock);
  CHECK((ev & WEBUP_EV_ERROR) && !strncmp(reply, "HTTP/1.1 400", 12) && strstr(reply, "Not music"));
  CHECK(exists(mdir, "b.ogg") < 0 && exists(mdir, "b.ogg.tmp") < 0);
  // GET on a route isn't an upload
  const char * getmusic = "GET /music HTTP/1.1\r\n\r\n";
  http(&w, port, getmusic, strlen(getmusic), reply, sizeof(reply), &clock);
  CHECK(!strncmp(reply, "HTTP/1.1 404", 12));

  // Bigger than both buffers: arrives intact
  size_t mlen = 2 * CHUNKWRITER_SIZE + 12345;
  char * mreq = malloc(mlen + 256);
  h = sprintf(mreq, "POST /upload?name=big HTTP/1.1\r\nContent-Length: %lu\r\n\r\n", (unsigned long)mlen);
  for(size_t i = 0; i < mlen; i++) mreq[h + i] = (char)(i * 7 + i / 1000);
  mreq[h] = (char)0xFF; mreq[h + 1] = (char)0xD8;
  ev = http(&w, port, mreq, h + mlen, reply, sizeof(reply), &clock);
  CHECK((ev & WEBUP_EV_SAVED) && strstr(reply, "Saved as big.jpg") != NULL);
  char bp[512];
  snprintf(bp, sizeof(bp), "%s/big.jpg", dir);
  FILE * bf = fopen(bp, "rb");
  char * back = malloc(mlen + 1);
  size_t bn = bf ? fread(back, 1, mlen + 1, bf) : 0;
  if(bf) fclose(bf);
  CHECK(bn == mlen && !memcmp(back, mreq + h, mlen));
  free(back);
  free(mreq);
  remove(bp);

  // A client that stops halfway is dropped after the timeout; its .tmp is removed
  int s = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  CHECK(connect(s, (struct sockaddr *)&a, sizeof(a)) == 0);
  const char * part = "POST /upload?name=stuck HTTP/1.1\r\nContent-Length: 1000\r\n\r\n\xFF\xD8partial";
  CHECK(send(s, part, strlen(part), MSG_NOSIGNAL) > 0);
  ev = 0;
  for(int i = 0; i < 1000; i++) ev |= webup_poll(&w, ++clock);
  CHECK((ev & WEBUP_EV_BUSY) && exists(dir, "stuck.jpg") < 0);
  clock += WEBUP_TIMEOUT_MS + 1;
  ev = webup_poll(&w, clock);
  CHECK((ev & WEBUP_EV_ERROR) && exists(dir, "stuck.jpg.tmp") < 0 && exists(dir, "stuck.jpg") < 0);
  close(s);
  // ...and the server still works after
  http(&w, port, get, strlen(get), reply, sizeof(reply), &clock);
  CHECK(!strncmp(reply, "HTTP/1.1 200", 12));

  webup_stop(&w);
  free(req);
  char p[512];
  snprintf(p, sizeof(p), "%s/Cat photo.jpg", dir); remove(p);
  snprintf(p, sizeof(p), "%s/Cat photo (2).jpg", dir); remove(p);
  rmdir(dir);
  snprintf(p, sizeof(p), "%s/Cat photo.mp3", mdir); remove(p);
  snprintf(p, sizeof(p), "%s/Cat photo (2).mp3", mdir); remove(p);
  rmdir(mdir);
}

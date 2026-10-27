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

static int webup_isjpeg(const char * path) {
  FILE * f = fopen(path, "rb");
  if(!f) return 0;
  unsigned char b[2] = {0};
  size_t n = fread(b, 1, 2, f);
  fclose(f);
  return n == 2 && b[0] == 0xFF && b[1] == 0xD8;
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

  char dir[] = "/tmp/jigsaw_webup_XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  const char page[] = "<html>upload page</html>";
  WebUp w;
  int port = 0;
  for(int p = 18000 + getpid() % 2000, tries = 0; tries < 20 && !port; p++, tries++) {
    if(!webup_start(&w, p, page, strlen(page), dir, webup_isjpeg)) port = p;
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

  // A client that stops halfway is dropped after the timeout; nothing reaches the SD (the
  // body is kept in RAM until complete)
  int s = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  CHECK(connect(s, (struct sockaddr *)&a, sizeof(a)) == 0);
  const char * part = "POST /upload?name=stuck HTTP/1.1\r\nContent-Length: 1000\r\n\r\n\xFF\xD8partial";
  CHECK(send(s, part, strlen(part), MSG_NOSIGNAL) > 0);
  ev = 0;
  for(int i = 0; i < 1000; i++) ev |= webup_poll(&w, ++clock);
  CHECK((ev & WEBUP_EV_BUSY) && exists(dir, "stuck.jpg.tmp") < 0);
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
}

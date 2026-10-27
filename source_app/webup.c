#include "webup.h"
#include "storage.h"
#include "picohttpparser.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define WEBUP_IDLE 0    // No client
#define WEBUP_HEAD 1    // Reading the request line and headers
#define WEBUP_BODY 2    // Saving an upload
#define WEBUP_SEND 3    // Sending the response
#define WEBUP_LINGER 4  // Response sent: reading (and dropping) whatever the client still sends
                        // until it hangs up. Closing with unread data or unsent bytes makes
                        // some stacks reset the connection, and the browser loses the reply.

#define WEBUP_MAXHEADERS 32

#define WEBUP_MSG_SET(w, ...) snprintf((w)->message, sizeof((w)->message), __VA_ARGS__)

static int webup_nonblock(int sock) {
  int flags = fcntl(sock, F_GETFL, 0);
  return flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0;
}

static int webup_wouldblock(void) {
  return errno == EAGAIN || errno == EWOULDBLOCK;
}

int webup_start(WebUp * w, int port, const char * page, size_t pagelen, const char * dir, webup_check_fn check) {
  memset(w, 0, sizeof(WebUp));
  w->lsock = w->csock = -1;
  w->page = page;
  w->pagelen = pagelen;
  w->check = check;
  snprintf(w->dir, sizeof(w->dir), "%s", dir);
  w->lsock = socket(AF_INET, SOCK_STREAM, 0);
  if(w->lsock < 0) {
    WEBUP_MSG_SET(w, "Can't open a network socket (%d)", errno);
    return 1;
  }
  int one = 1;
  setsockopt(w->lsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));  // Fine if unsupported
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if(bind(w->lsock, (struct sockaddr *)&addr, sizeof(addr)) || listen(w->lsock, 4) || webup_nonblock(w->lsock)) {
    WEBUP_MSG_SET(w, "Can't start the server on port %d (%d)", port, errno);
    close(w->lsock);
    w->lsock = -1;
    return 1;
  }
  return 0;
}

// Drop the client; an unfinished upload is thrown away
static void webup_drop(WebUp * w) {
  free(w->body);
  w->body = NULL;
  if(w->csock >= 0) close(w->csock);
  w->csock = -1;
  w->state = WEBUP_IDLE;
}

void webup_stop(WebUp * w) {
  webup_drop(w);
  if(w->lsock >= 0) close(w->lsock);
  w->lsock = -1;
}

size_t webup_urldecode(const char * src, size_t len, char * out, size_t outlen) {
  size_t n = 0;
  for(size_t i = 0; i < len && n + 1 < outlen; i++) {
    char c = src[i];
    if(c == '+') c = ' ';
    else if(c == '%' && i + 2 < len) {
      int v = 0, ok = 1;
      for(int k = 1; k <= 2; k++) {
        char h = src[i + k];
        v <<= 4;
        if(h >= '0' && h <= '9') v |= h - '0';
        else if(h >= 'a' && h <= 'f') v |= h - 'a' + 10;
        else if(h >= 'A' && h <= 'F') v |= h - 'A' + 10;
        else ok = 0;
      }
      if(ok) {
        c = (char)v;
        i += 2;
      }
    }
    out[n++] = c;
  }
  out[n] = 0;
  return n;
}

int webup_query(const char * path, size_t pathlen, const char * key, char * out, size_t outlen) {
  const char * q = memchr(path, '?', pathlen);
  if(!q) return 1;
  const char * end = path + pathlen;
  size_t klen = strlen(key);
  for(const char * p = q + 1; p < end; ) {
    const char * amp = memchr(p, '&', end - p);
    if(!amp) amp = end;
    if((size_t)(amp - p) > klen && !memcmp(p, key, klen) && p[klen] == '=') {
      webup_urldecode(p + klen + 1, amp - p - klen - 1, out, outlen);
      return 0;
    }
    p = amp + 1;
  }
  return 1;
}

// Queue a response (short text body, then optionally extra bytes) and switch to sending
static void webup_respond(WebUp * w, int code, const char * reason, const char * type, const char * text,
    const char * extra, size_t extralen) {
  size_t textlen = strlen(text);
  int n = snprintf(w->out, sizeof(w->out), "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
      "Connection: close\r\nCache-Control: no-store\r\n\r\n%s", code, reason, type,
      (unsigned long)(textlen + extralen), text);
  w->outlen = n < 0 ? 0 : (size_t)n < sizeof(w->out) ? (size_t)n : sizeof(w->out) - 1;
  w->outsent = 0;
  w->extra = extra;
  w->extralen = extralen;
  w->state = WEBUP_SEND;
}

static int webup_fail(WebUp * w, int code, const char * reason, const char * why) {
  WEBUP_MSG_SET(w, "%s", why);
  webup_respond(w, code, reason, "text/plain; charset=utf-8", why, NULL, 0);
  return WEBUP_EV_ERROR;
}

// Whole upload received (in RAM): write it out, keep it if it's a usable picture
static int webup_finish(WebUp * w) {
  char path[STORAGE_MAXPATH], tmp[STORAGE_MAXTMP];
  snprintf(path, sizeof(path), "%s/%s", w->dir, w->name);
  storage_tmppath(path, tmp);
  FILE * f = fopen(tmp, "wb");
  int ok = f && fwrite(w->body, 1, w->expect, f) == w->expect;
  if(f && fclose(f)) ok = 0;
  free(w->body);
  w->body = NULL;
  if(!ok) {
    storage_commit(path, 0);
    return webup_fail(w, 500, "Internal Server Error", "Couldn't write to the SD card (full?)");
  }
  if(w->check && !w->check(tmp)) {
    storage_commit(path, 0);
    return webup_fail(w, 400, "Bad Request", "That isn't a picture the 3DS can open");
  }
  if(storage_commit(path, 1)) return webup_fail(w, 500, "Internal Server Error", "Couldn't save on the SD card");
  WEBUP_MSG_SET(w, "%s", w->name);
  char text[200];
  snprintf(text, sizeof(text), "Saved as %s", w->name);
  webup_respond(w, 200, "OK", "text/plain; charset=utf-8", text, NULL, 0);
  return WEBUP_EV_SAVED;
}

// Body bytes that came with the headers; finishes when all are in
static int webup_body(WebUp * w, const char * data, size_t len) {
  if(len > w->expect - w->received) len = w->expect - w->received;
  memcpy(w->body + w->received, data, len);
  w->received += len;
  return w->received >= w->expect ? webup_finish(w) : WEBUP_EV_BUSY;
}

// Headers are in (hlen bytes of head): decide what to do
static int webup_route(WebUp * w, size_t hlen, const char * method, size_t mlen, const char * path, size_t plen,
    struct phr_header * hd, size_t nh) {
  size_t qlen = plen;
  const char * q = memchr(path, '?', plen);
  if(q) qlen = q - path;
  int get = mlen == 3 && !memcmp(method, "GET", 3), post = mlen == 4 && !memcmp(method, "POST", 4);
  if(get && ((qlen == 1 && path[0] == '/') || (qlen == 11 && !memcmp(path, "/index.html", 11)))) {
    webup_respond(w, 200, "OK", "text/html; charset=utf-8", "", w->page, w->pagelen);
    return 0;
  }
  if(!(post && qlen == 7 && !memcmp(path, "/upload", 7))) {
    webup_respond(w, 404, "Not Found", "text/plain", "Not found", NULL, 0);
    return 0;
  }
  // Upload: needs a length, within limits
  long long length = -1;
  for(size_t i = 0; i < nh; i++) {
    if(hd[i].name_len != 14 || strncasecmp(hd[i].name, "content-length", 14)) continue;
    length = 0;
    for(size_t k = 0; k < hd[i].value_len; k++) {
      char c = hd[i].value[k];
      if(c < '0' || c > '9' || length > WEBUP_MAXBODY) { length = -2; break; }
      length = length * 10 + (c - '0');
    }
  }
  if(length == -1) return webup_fail(w, 411, "Length Required", "Upload had no length");
  if(length < 0 || length > WEBUP_MAXBODY) return webup_fail(w, 413, "Payload Too Large", "Picture too big");
  if(length == 0) return webup_fail(w, 400, "Bad Request", "Upload was empty");
  char raw[256], base[STORAGE_MAXBASE + 1];
  if(webup_query(path, plen, "name", raw, sizeof(raw))) raw[0] = 0;
  storage_cleanname(raw, base, sizeof(base));
  if(storage_freename(w->dir, base, ".jpg", w->name, sizeof(w->name))) {
    return webup_fail(w, 409, "Conflict", "Too many pictures with that name");
  }
  // The body goes to RAM and is written in one go at the end: SD writes while receiving
  // stalled the 3DS's tiny (~8 KB) receive window. length was checked against WEBUP_MAXBODY.
  w->body = malloc(length);
  if(!w->body) return webup_fail(w, 503, "Service Unavailable", "Not enough memory for that picture");
  w->expect = length;
  w->received = 0;
  w->state = WEBUP_BODY;
  // Body bytes that came in with the headers
  return webup_body(w, w->head + hlen, w->headlen - hlen);
}

// One step of the client's state; returns events, sets *moved to bytes moved (0 = nothing
// to do right now)
static int webup_step(WebUp * w, uint64_t now, size_t * moved) {
  *moved = 0;
  if(w->state == WEBUP_HEAD) {
    ssize_t n = recv(w->csock, w->head + w->headlen, WEBUP_MAXHEAD - w->headlen, 0);
    if(n == 0 || (n < 0 && !webup_wouldblock())) {
      webup_drop(w);
      return 0;
    }
    if(n < 0) return 0;
    w->headlen += n;
    w->head[w->headlen] = 0;
    w->last = now;
    *moved = n;
    const char * method, * path;
    size_t mlen, plen, nh = WEBUP_MAXHEADERS;
    int minor;
    struct phr_header hd[WEBUP_MAXHEADERS];
    int r = phr_parse_request(w->head, w->headlen, &method, &mlen, &path, &plen, &minor, hd, &nh, 0);
    if(r == -1) {
      webup_respond(w, 400, "Bad Request", "text/plain", "Bad request", NULL, 0);
      return 0;
    }
    if(r == -2) {
      if(w->headlen >= WEBUP_MAXHEAD) webup_respond(w, 431, "Request Header Fields Too Large", "text/plain", "Too big", NULL, 0);
      return 0;
    }
    return webup_route(w, r, method, mlen, path, plen, hd, nh);
  }
  if(w->state == WEBUP_BODY) {
    size_t want = w->expect - w->received < WEBUP_CHUNK ? w->expect - w->received : WEBUP_CHUNK;
    ssize_t n = recv(w->csock, w->body + w->received, want, 0);
    if(n == 0 || (n < 0 && !webup_wouldblock())) {
      WEBUP_MSG_SET(w, "Upload cut off");
      webup_drop(w);
      return WEBUP_EV_ERROR;
    }
    if(n < 0) return 0;
    w->last = now;
    *moved = n;
    w->received += n;
    return w->received >= w->expect ? webup_finish(w) : WEBUP_EV_BUSY;
  }
  if(w->state == WEBUP_SEND) {
    const char * data;
    size_t left;
    if(w->outsent < w->outlen) {
      data = w->out + w->outsent;
      left = w->outlen - w->outsent;
    } else {
      size_t done = w->outsent - w->outlen;
      data = w->extra + done;
      left = w->extralen - done;
    }
    if(left == 0) {
      shutdown(w->csock, SHUT_WR);
      w->state = WEBUP_LINGER;
      w->last = now;
      return 0;
    }
    if(left > WEBUP_CHUNK) left = WEBUP_CHUNK;
    ssize_t n = send(w->csock, data, left, MSG_NOSIGNAL);
    if(n < 0 && webup_wouldblock()) return 0;
    if(n <= 0) {
      webup_drop(w);
      return 0;
    }
    w->outsent += n;
    w->last = now;
    *moved = n;
  }
  if(w->state == WEBUP_LINGER) {
    char buf[1024];
    ssize_t n = recv(w->csock, buf, sizeof(buf), 0);
    if(n == 0 || (n < 0 && !webup_wouldblock()) || now - w->last > WEBUP_LINGER_MS) webup_drop(w);
    else if(n > 0) *moved = n;
  }
  return 0;
}

int webup_poll(WebUp * w, uint64_t now) {
  if(w->lsock < 0) return 0;
  int ev = 0;
  if(w->state == WEBUP_IDLE) {
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    int s = accept(w->lsock, (struct sockaddr *)&addr, &alen);
    if(s < 0) return 0;
    if(webup_nonblock(s)) {
      close(s);
      return 0;
    }
    w->csock = s;
    w->state = WEBUP_HEAD;
    w->headlen = 0;
    w->last = now;
  }
  size_t total = 0, moved;
  do {
    ev |= webup_step(w, now, &moved);
    total += moved;
  } while(moved && w->state != WEBUP_IDLE && total < WEBUP_FRAMEBYTES);
  if(w->state != WEBUP_IDLE && now - w->last > WEBUP_TIMEOUT_MS) {
    if(w->state == WEBUP_BODY) {
      WEBUP_MSG_SET(w, "Upload stopped (timed out)");
      ev |= WEBUP_EV_ERROR;
    }
    webup_drop(w);
  }
  if(w->state == WEBUP_BODY) ev |= WEBUP_EV_BUSY;
  return ev;
}

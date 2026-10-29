#include "upload.h"
#include "imageload.h"
#include "musicdec.h"
#include "speedup.h"
#include "storage.h"

#include <citro2d.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define UPLOAD_WHITE C2D_Color32(230, 230, 230, 255)
#define UPLOAD_DIM   C2D_Color32(150, 150, 150, 255)
#define UPLOAD_TITLE C2D_Color32(240, 200, 90, 255)
#define UPLOAD_GOOD  C2D_Color32(143, 216, 143, 255)
#define UPLOAD_BAD   C2D_Color32(255, 140, 120, 255)

static void upload_line(Upload * u, int good, const char * fmt, const char * text) {
  if(u->nlines == UPLOAD_LINES) {
    memmove(u->lines, u->lines + 1, sizeof(u->lines[0]) * (UPLOAD_LINES - 1));
    memmove(u->linegood, u->linegood + 1, UPLOAD_LINES - 1);
    u->nlines--;
  }
  snprintf(u->lines[u->nlines], sizeof(u->lines[0]), fmt, text);
  u->linegood[u->nlines++] = good;
}

static int upload_check(const char * path, const char * name) {
  return imageload_check(path);
}

// Music: opens and decodes a little (the kind comes from the final name; path is a .tmp)
static int upload_musiccheck(const char * path, const char * name) {
  static int16_t pcm[2 * 4096];  // Only the server thread checks
  MusicDec d;
  if(musicdec_open(&d, path, musicdec_kind(name), NULL)) return 0;
  uint32_t got = musicdec_read(&d, pcm, 4096);
  musicdec_close(&d);
  return got > 0;
}

static const char * const upload_musicexts[] = { ".mp3", ".flac", ".ogg", NULL };
static const WebUpRoute upload_routes[] = {
  { "/upload", STORAGE_PUZZLES, ".jpg", NULL, upload_check, "That isn't a picture the 3DS can open" },
  { "/music", STORAGE_MUSIC, NULL, upload_musicexts, upload_musiccheck, "That isn't music the 3DS can play" },
};

// The page, read once from romfs
static void upload_loadpage(Upload * u) {
  if(u->page || R_FAILED(romfsInit())) return;
  FILE * f = fopen(UPLOAD_PAGE, "rb");
  if(f) {
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    u->page = len > 0 ? malloc(len) : NULL;
    if(u->page && fread(u->page, 1, len, f) == (size_t)len) u->pagelen = len;
    else {
      free(u->page);
      u->page = NULL;
    }
    fclose(f);
  }
  romfsExit();
}

// One server step, then (under the lock) its events and a copy of the progress for the
// screen. webup itself is only touched by the polling thread.
static int upload_poll(Upload * u) {
  int ev = webup_poll(&u->web, osGetTime());
  LightLock_Lock(&u->lock);
  u->busy = (ev & WEBUP_EV_BUSY) != 0;
  snprintf(u->rxname, sizeof(u->rxname), "%s", u->web.name);
  u->rxgot = u->web.received;
  u->rxtotal = u->web.expect;
  u->rxsdwait = u->web.sdwait;
  if(ev & WEBUP_EV_SAVED) {
    if(u->web.route == upload_routes) u->added++;
    upload_line(u, 1, "Added %s", u->web.message);
  }
  if(ev & WEBUP_EV_ERROR) upload_line(u, 0, "%s", u->web.message);
  LightLock_Unlock(&u->lock);
  return u->web.csock >= 0;
}

static void upload_thread(void * arg) {
  Upload * u = arg;
  // Not sleeping with a client (netloader style, same priority as main) was tried: no
  // faster (2026-10-29)
  while(!u->quit) {
    svcSleepThread(upload_poll(u) ? UPLOAD_CLIENT_NS : UPLOAD_IDLE_NS);
  }
}

static void upload_stopthread(Upload * u) {
  if(!u->thread) return;
  u->quit = 1;
  threadJoin(u->thread, U64_MAX);
  threadFree(u->thread);
  u->thread = NULL;
}

static void upload_stopnet(Upload * u) {
  upload_stopthread(u);
  if(u->running) webup_stop(&u->web);
  u->running = 0;
  if(u->socok) socExit();
  u->socok = 0;
}

// Network up, address known, server listening (or problem says why not)
static void upload_startnet(Upload * u) {
  u->lastretry = osGetTime();
  if(!u->page) {
    snprintf(u->problem, sizeof(u->problem), "Can't find the upload page in the app");
    return;
  }
  if(!u->socbuf) u->socbuf = memalign(0x1000, UPLOAD_SOCBUF);
  if(!u->socbuf) {
    snprintf(u->problem, sizeof(u->problem), "Not enough memory for networking");
    return;
  }
  if(!u->socok) {
    if(R_FAILED(socInit(u->socbuf, UPLOAD_SOCBUF))) {
      snprintf(u->problem, sizeof(u->problem), "Can't start networking");
      return;
    }
    u->socok = 1;
  }
  struct in_addr ip = { .s_addr = gethostid() };
  if(ip.s_addr == 0) {
    snprintf(u->problem, sizeof(u->problem), "Not connected to Wi-Fi (still trying)");
    return;
  }
  if(webup_start(&u->web, UPLOAD_PORT, u->page, u->pagelen, upload_routes, 2)) {
    snprintf(u->problem, sizeof(u->problem), "%s", u->web.message);
    return;
  }
  u->running = 1;
  u->problem[0] = 0;
  // Higher priority than the main thread (it mostly sleeps), same core. If it can't be
  // made, upload_update polls once a frame instead.
  s32 prio = 0x30;
  svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  u->quit = 0;
  u->thread = threadCreate(upload_thread, u, UPLOAD_STACK, prio - 1, -2, false);
  snprintf(u->url, sizeof(u->url), "http://%s/", inet_ntoa(ip));
  uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
  u->qrok = qrcodegen_encodeText(u->url, tmp, u->qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
      qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true);
}

void upload_open(Upload * u) {
  memset(u, 0, sizeof(Upload));
#ifndef UPLOAD_NOSPEEDUP
  speedup_begin();  // Whole visit (1.3 test: does it help the upload rate?)
#endif
  LightLock_Init(&u->lock);
  upload_loadpage(u);
  upload_startnet(u);
}

void upload_close(Upload * u) {
  upload_stopnet(u);
  free(u->socbuf);
  free(u->page);
  u->socbuf = NULL;
  u->page = NULL;
#ifndef UPLOAD_NOSPEEDUP
  speedup_end();
#endif
}

void upload_pause(Upload * u) {
  if(u->paused) return;
  upload_stopnet(u);
  u->paused = 1;
}

void upload_resume(Upload * u) {
  if(!u->paused) return;
  u->paused = 0;
  u->lastretry = 0;   // Start again on the next update
}

int upload_update(Upload * u, u32 down) {
  if(down & (KEY_B | KEY_START)) return 1;
  u64 now = osGetTime();
  if(!u->running && !u->paused && now - u->lastretry >= UPLOAD_RETRY_MS) upload_startnet(u);
  if(u->running && !u->thread) upload_poll(u);
  return 0;
}

void upload_render_top(Upload * u, TextDraw * td) {
  textdraw_print(td, 10, 6, 0.6f, UPLOAD_TITLE, "Add pictures and music over Wi-Fi");
  if(u->running) {
    textdraw_print(td, 10, 34, 0.5f, UPLOAD_DIM, "On a phone or computer on the same Wi-Fi, open:");
    textdraw_center(td, 200, 52, 0.9f, UPLOAD_WHITE, "%s", u->url);
    textdraw_print(td, 10, 84, 0.5f, UPLOAD_DIM, "or scan the code on the bottom screen.");
  } else {
    textdraw_print(td, 10, 40, 0.5f, UPLOAD_BAD, "%s", u->paused ? "Paused" : u->problem);
  }
  LightLock_Lock(&u->lock);
  float y = 108;
  if(u->busy && u->rxtotal) {
    textdraw_print(td, 10, y, 0.5f, UPLOAD_WHITE, "Receiving %s... %d%%%s", u->rxname,
        (int)(100.0 * u->rxgot / u->rxtotal), u->rxsdwait ? " (waiting for SD)" : "");
  }
  y += 18;
  for(int i = 0; i < u->nlines; i++, y += 15) {
    textdraw_print(td, 10, y, 0.45f, u->linegood[i] ? UPLOAD_GOOD : UPLOAD_BAD, "%s", u->lines[i]);
  }
  textdraw_print(td, 10, 222, 0.5f, UPLOAD_DIM, "B: done%s", u->added ? " (new pictures are in the list)" : "");
  LightLock_Unlock(&u->lock);
}

void upload_render_bottom(Upload * u, TextDraw * td) {
  if(!u->running || !u->qrok) {
    textdraw_center(td, 160, 110, 0.5f, UPLOAD_DIM, u->running ? "Open the address on the top screen" : "Waiting for Wi-Fi");
    return;
  }
  // White square with a 4-module quiet zone, black modules, as big as fits
  int n = qrcodegen_getSize(u->qr), full = n + 8;
  int scale = 220 / full;
  if(scale < 1) scale = 1;
  float side = full * scale, x0 = (320 - side) / 2, y0 = (240 - side) / 2;
  C2D_DrawRectSolid(x0, y0, 0.5f, side, side, C2D_Color32(255, 255, 255, 255));
  for(int y = 0; y < n; y++) {
    for(int x = 0; x < n; x++) {
      if(!qrcodegen_getModule(u->qr, x, y)) continue;
      C2D_DrawRectSolid(x0 + (x + 4) * scale, y0 + (y + 4) * scale, 0.6f, scale, scale, C2D_Color32(0, 0, 0, 255));
    }
  }
}

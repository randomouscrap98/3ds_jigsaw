#include "textdraw.h"

#include <stdarg.h>
#include <stdio.h>

int textdraw_init(TextDraw * td) {
  td->buf = C2D_TextBufNew(TEXTDRAW_BUFSIZE);
  return td->buf ? 0 : 1;
}

void textdraw_free(TextDraw * td) {
  if(td->buf) C2D_TextBufDelete(td->buf);
  td->buf = NULL;
}

void textdraw_begin(TextDraw * td) {
  C2D_TextBufClear(td->buf);
}

static void textdraw_draw(TextDraw * td, float x, float y, float scale, u32 color, int center,
    const char * fmt, va_list args) {
  char line[TEXTDRAW_MAXLINE];
  vsnprintf(line, sizeof(line), fmt, args);
  C2D_Text text;
  if(!C2D_TextParse(&text, td->buf, line)) return;
  C2D_TextOptimize(&text);
  u32 flags = C2D_WithColor;
  if(center) flags |= C2D_AlignCenter;
  C2D_DrawText(&text, flags, x, y, 0.9f, scale, scale, color);
}

void textdraw_print(TextDraw * td, float x, float y, float scale, u32 color, const char * fmt, ...) {
  va_list args;
  va_start(args, fmt);
  textdraw_draw(td, x, y, scale, color, 0, fmt, args);
  va_end(args);
}

void textdraw_center(TextDraw * td, float x, float y, float scale, u32 color, const char * fmt, ...) {
  va_list args;
  va_start(args, fmt);
  textdraw_draw(td, x, y, scale, color, 1, fmt, args);
  va_end(args);
}

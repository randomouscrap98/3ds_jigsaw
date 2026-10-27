#include "textdraw.h"

#include <math.h>
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

void textdraw_marquee(TextDraw * td, float x, float y, float w, float scale, u32 color,
    float scroll, const char * str) {
  C2D_Text text;
  if(!C2D_TextParse(&text, td->buf, str)) return;
  C2D_TextOptimize(&text);
  float tw, th;
  C2D_TextGetDimensions(&text, scale, scale, &tw, &th);
  if(tw <= w) {
    C2D_DrawText(&text, C2D_WithColor | C2D_AlignCenter, x + w / 2, y, 0.9f, scale, scale, color);
    return;
  }
  float period = tw + TEXTDRAW_MARQUEE_GAP;
  float off = fmodf(scroll, period);
  // The scissor box is in framebuffer coordinates; the top screen's framebuffer is
  // rotated (240 wide, 400 tall), so screen x runs down it from the far end
  float x0 = x, y0 = y, x1 = x + w, y1 = y + th;
  if(x0 < 0) x0 = 0;
  if(y0 < 0) y0 = 0;
  if(x1 > TEXTDRAW_TOPW) x1 = TEXTDRAW_TOPW;
  if(y1 > TEXTDRAW_TOPH) y1 = TEXTDRAW_TOPH;
  C2D_Flush();  // Whatever is already batched mustn't be clipped
  C3D_SetScissor(GPU_SCISSOR_NORMAL, (u32)(TEXTDRAW_TOPH - y1), (u32)(TEXTDRAW_TOPW - x1),
      (u32)(TEXTDRAW_TOPH - y0), (u32)(TEXTDRAW_TOPW - x0));
  C2D_DrawText(&text, C2D_WithColor, x - off, y, 0.9f, scale, scale, color);
  C2D_DrawText(&text, C2D_WithColor, x - off + period, y, 0.9f, scale, scale, color);
  C2D_Flush();
  C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
}

void textdraw_fmttime(char * out, size_t len, uint32_t seconds) {
  unsigned long h = seconds / 3600, m = seconds / 60 % 60, s = seconds % 60;
  if(h) snprintf(out, len, "%lu:%02lu:%02lu", h, m, s);
  else snprintf(out, len, "%lu:%02lu", m, s);
}

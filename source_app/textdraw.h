#ifndef __HEADER_JIGSAW_TEXTDRAW__
#define __HEADER_JIGSAW_TEXTDRAW__

#include <citro2d.h>

#define TEXTDRAW_BUFSIZE 4096
#define TEXTDRAW_MAXLINE 256

// Immediate-mode text with the system font. Call textdraw_begin once per frame, then
// print as needed while a target is active.
typedef struct {
  C2D_TextBuf buf;
} TextDraw;

int textdraw_init(TextDraw * td);
void textdraw_free(TextDraw * td);
void textdraw_begin(TextDraw * td);
// printf-style text at (x, y), top-left anchored
void textdraw_print(TextDraw * td, float x, float y, float scale, u32 color, const char * fmt, ...)
  __attribute__((format(printf, 6, 7)));
// Same but horizontally centered on x
void textdraw_center(TextDraw * td, float x, float y, float scale, u32 color, const char * fmt, ...)
  __attribute__((format(printf, 6, 7)));

// Format a duration as m:ss, or h:mm:ss from an hour up
void textdraw_fmttime(char * out, size_t len, uint32_t seconds);

#endif

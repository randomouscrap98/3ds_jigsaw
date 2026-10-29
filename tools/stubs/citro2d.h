#pragma once
#include "citro3d.h"
typedef struct { u16 width, height; float left, top, right, bottom; } Tex3DS_SubTexture;
typedef struct { C3D_Tex * tex; const Tex3DS_SubTexture * subtex; } C2D_Image;
typedef struct { u32 color; float blend; } C2D_Tint;
typedef struct { C2D_Tint corners[4]; } C2D_ImageTint;
typedef struct C2D_TextBuf_s * C2D_TextBuf;
typedef struct { C2D_TextBuf buf; size_t begin, end; float width; u32 lines, words; void * font; } C2D_Text;
enum { C2D_AtBaseline = 1, C2D_WithColor = 2, C2D_AlignLeft = 0, C2D_AlignRight = 4, C2D_AlignCenter = 8 };
#define C2D_DEFAULT_MAX_OBJECTS 4096
static inline u32 C2D_Color32(u8 r, u8 g, u8 b, u8 a) { return r | (g << 8) | (b << 16) | ((u32)a << 24); }
bool C2D_Init(size_t maxObjects); void C2D_Fini(void); void C2D_Prepare(void); void C2D_Flush(void);
void C2D_TextGetDimensions(const C2D_Text * text, float scaleX, float scaleY, float * outWidth, float * outHeight);
C3D_RenderTarget * C2D_CreateScreenTarget(gfxScreen_t screen, gfx3dSide_t side);
void C2D_TargetClear(C3D_RenderTarget * target, u32 color);
void C2D_SceneBegin(C3D_RenderTarget * target);
bool C2D_DrawImageAt(C2D_Image img, float x, float y, float depth, const C2D_ImageTint * tint, float scaleX, float scaleY);
bool C2D_DrawImageAtRotated(C2D_Image img, float x, float y, float depth, float angle, const C2D_ImageTint * tint, float scaleX, float scaleY);
bool C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 clr);
bool C2D_DrawTriangle(float x0, float y0, u32 clr0, float x1, float y1, u32 clr1, float x2, float y2, u32 clr2, float depth);
bool C2D_DrawLine(float x0, float y0, u32 clr0, float x1, float y1, u32 clr1, float thickness, float depth);
bool C2D_DrawCircleSolid(float x, float y, float z, float radius, u32 clr);
void C2D_PlainImageTint(C2D_ImageTint * tint, u32 color, float blend);
C2D_TextBuf C2D_TextBufNew(size_t maxGlyphs); void C2D_TextBufDelete(C2D_TextBuf buf); void C2D_TextBufClear(C2D_TextBuf buf);
const char * C2D_TextParse(C2D_Text * text, C2D_TextBuf buf, const char * str);
void C2D_TextOptimize(const C2D_Text * text);
void C2D_DrawText(const C2D_Text * text, u32 flags, float x, float y, float z, float scaleX, float scaleY, ...);

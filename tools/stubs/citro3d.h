#pragma once
#include "3ds.h"
typedef enum { GPU_RGBA8 = 0 } GPU_TEXCOLOR;
typedef enum { GPU_NEAREST, GPU_LINEAR } GPU_TEXTURE_FILTER_PARAM;
typedef enum { GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_BORDER, GPU_REPEAT } GPU_TEXTURE_WRAP_PARAM;
typedef struct { void * data; GPU_TEXCOLOR fmt : 4; size_t size : 28; u16 width, height; } C3D_Tex;
typedef struct C3D_RenderTarget_tag C3D_RenderTarget;
#define C3D_DEFAULT_CMDBUF_SIZE 0x40000
#define C3D_FRAME_SYNCDRAW 1
bool C3D_Init(size_t cmdBufSize); void C3D_Fini(void);
bool C3D_FrameBegin(u8 flags); void C3D_FrameEnd(u8 flags);
float C3D_GetDrawingTime(void); float C3D_GetProcessingTime(void);
bool C3D_TexInit(C3D_Tex * tex, u16 width, u16 height, GPU_TEXCOLOR format);
void C3D_TexDelete(C3D_Tex * tex); void C3D_TexFlush(C3D_Tex * tex);
void C3D_TexSetFilter(C3D_Tex * tex, GPU_TEXTURE_FILTER_PARAM magFilter, GPU_TEXTURE_FILTER_PARAM minFilter);
void C3D_TexSetWrap(C3D_Tex * tex, GPU_TEXTURE_WRAP_PARAM wrapS, GPU_TEXTURE_WRAP_PARAM wrapT);

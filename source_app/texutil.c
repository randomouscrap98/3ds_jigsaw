#include "texutil.h"

int texutil_init(C3D_Tex * t, uint16_t w, uint16_t h, int clamp) {
  if(!C3D_TexInit(t, w, h, GPU_RGBA8)) return 1;
  C3D_TexSetFilter(t, GPU_LINEAR, GPU_LINEAR);
  if(clamp) C3D_TexSetWrap(t, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
  return 0;
}

Tex3DS_SubTexture texutil_subtex(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t texw, uint16_t texh) {
  return (Tex3DS_SubTexture) {
    .width = w,
    .height = h,
    .left = (float)x / texw,
    .top = 1.0f - (float)y / texh,
    .right = (float)(x + w) / texw,
    .bottom = 1.0f - (float)(y + h) / texh,
  };
}

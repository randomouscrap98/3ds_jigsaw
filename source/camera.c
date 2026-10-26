#include "camera.h"

static void camera_clamp(Camera * c) {
  if(c->zoom < c->minzoom) c->zoom = c->minzoom;
  if(c->zoom > c->maxzoom) c->zoom = c->maxzoom;
  if(c->cx < 0) c->cx = 0;
  if(c->cy < 0) c->cy = 0;
  if(c->cx > c->bw) c->cx = c->bw;
  if(c->cy > c->bh) c->cy = c->bh;
}

void camera_init(Camera * c, float sw, float sh, float bw, float bh, float maxzoom) {
  c->sw = sw;
  c->sh = sh;
  c->bw = bw;
  c->bh = bh;
  float zx = sw / bw, zy = sh / bh;
  c->minzoom = zx < zy ? zx : zy;
  c->maxzoom = maxzoom > c->minzoom ? maxzoom : c->minzoom;
  camera_set(c, bw / 2, bh / 2, c->minzoom);
}

void camera_set(Camera * c, float cx, float cy, float zoom) {
  c->cx = cx;
  c->cy = cy;
  c->zoom = zoom;
  camera_clamp(c);
}

void camera_pan(Camera * c, float dx, float dy) {
  c->cx += dx / c->zoom;
  c->cy += dy / c->zoom;
  camera_clamp(c);
}

void camera_zoom(Camera * c, float factor) {
  c->zoom *= factor;
  camera_clamp(c);
}

void camera_to_world(Camera * c, float sx, float sy, float * wx, float * wy) {
  *wx = (sx - c->sw / 2) / c->zoom + c->cx;
  *wy = (sy - c->sh / 2) / c->zoom + c->cy;
}

void camera_to_screen(Camera * c, float wx, float wy, float * sx, float * sy) {
  *sx = (wx - c->cx) * c->zoom + c->sw / 2;
  *sy = (wy - c->cy) * c->zoom + c->sh / 2;
}

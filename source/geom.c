#include "geom.h"

#include <math.h>

int geom_inside(const float * xy, uint32_t n, float x, float y) {
  int in = 0;
  for(uint32_t i = 0, j = n - 1; i < n; j = i++) {
    float xi = xy[2 * i], yi = xy[2 * i + 1], xj = xy[2 * j], yj = xy[2 * j + 1];
    if((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
  }
  return in;
}

float geom_area(const float * xy, uint32_t n, float * cx, float * cy) {
  double a = 0, x = 0, y = 0;
  for(uint32_t i = 0; i < n; i++) {
    const float * u = xy + 2 * i, * v = xy + 2 * ((i + 1) % n);
    double c = (double)u[0] * v[1] - (double)v[0] * u[1];
    a += c;
    x += (u[0] + v[0]) * c;
    y += (u[1] + v[1]) * c;
  }
  a /= 2;
  if(cx && a != 0) {
    *cx = x / (6 * a);
    *cy = y / (6 * a);
  }
  return a;
}

float geom_pointseg(float px, float py, float ax, float ay, float bx, float by) {
  float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
  float l = vx * vx + vy * vy;
  float t = l > 0 ? (vx * wx + vy * wy) / l : 0;
  if(t < 0) t = 0;
  if(t > 1) t = 1;
  float dx = wx - t * vx, dy = wy - t * vy;
  return sqrtf(dx * dx + dy * dy);
}

#include "stick.h"

#include <math.h>

void stick_init(Stick * s, float range, float deadzone, float power) {
  s->range = range;
  s->deadzone = deadzone;
  s->power = power;
}

int stick_read(const Stick * s, float dx, float dy, float * ox, float * oy) {
  float len = sqrtf(dx * dx + dy * dy);
  float m = len / s->range;
  if(m > 1) m = 1;
  if(len <= 0 || m <= s->deadzone) {
    *ox = *oy = 0;
    return 0;
  }
  // Remap so the curve starts from 0 right at the deadzone edge (no jump)
  float t = powf((m - s->deadzone) / (1 - s->deadzone), s->power);
  *ox = dx / len * t;
  *oy = dy / len * t;
  return 1;
}

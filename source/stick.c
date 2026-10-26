#include "stick.h"

#include <math.h>

void stick_init(Stick * s, float range, float deadzone, float power, float min) {
  s->range = range;
  s->deadzone = deadzone;
  s->power = power;
  s->min = min;
}

int stick_read(const Stick * s, float dx, float dy, float * ox, float * oy) {
  float len = sqrtf(dx * dx + dy * dy);
  if(len <= s->deadzone || s->range <= s->deadzone) {
    *ox = *oy = 0;
    return 0;
  }
  float m = (len - s->deadzone) / (s->range - s->deadzone);
  if(m > 1) m = 1;
  float t = s->min + (1 - s->min) * powf(m, s->power);
  *ox = dx / len * t;
  *oy = dy / len * t;
  return 1;
}

#ifndef __HEADER_JIGSAW_STICK__
#define __HEADER_JIGSAW_STICK__

// Analog stick response: raw stick values (about -156..156 on the 3DS) to a direction
// scaled 0..1, with a deadzone and a curve so small tilts are slow and precise.
// Just past the deadzone the output is `min`; it rises to 1 at full tilt along tilt^power.
typedef struct {
  float range;      // Raw value that counts as full tilt
  float deadzone;   // Raw value ignored around the center (checked on total tilt, not per axis)
  float power;      // Response curve exponent (1 = linear, 2 = quadratic)
  float min;        // Output just past the deadzone (0..1)
} Stick;

void stick_init(Stick * s, float range, float deadzone, float power, float min);
// Output vector has length min..1 in the stick's direction. Returns 0 (and a zero
// vector) inside the deadzone.
int stick_read(const Stick * s, float dx, float dy, float * ox, float * oy);

#endif

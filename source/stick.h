#ifndef __HEADER_JIGSAW_STICK__
#define __HEADER_JIGSAW_STICK__

// Analog stick response: raw stick values (about -156..156 on the 3DS) to a direction
// scaled 0..1, with a deadzone and a curve so small tilts are slow and precise.
typedef struct {
  float range;      // Raw value that counts as full tilt
  float deadzone;   // Fraction of range ignored around the center
  float power;      // Response curve exponent (1 = linear, 2 = quadratic)
} Stick;

void stick_init(Stick * s, float range, float deadzone, float power);
// Output vector has length 0..1 in the stick's direction. Returns 0 if inside the deadzone.
int stick_read(const Stick * s, float dx, float dy, float * ox, float * oy);

#endif

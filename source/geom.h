#ifndef __HEADER_JIGSAW_GEOM__
#define __HEADER_JIGSAW_GEOM__

#include <stdint.h>

// Small polygon helpers shared by the cut code. A polygon is n (x, y) pairs, closed
// implicitly (the last point connects back to the first).

// Even-odd point in polygon. Half-open: a point on a left or top edge is inside, on a
// right or bottom edge outside, so polygons sharing an edge never both claim a point.
int geom_inside(const float * xy, uint32_t n, float x, float y);
// Signed area (positive = clockwise on screen, y down), and the centroid if cx is given
// and the area isn't 0
float geom_area(const float * xy, uint32_t n, float * cx, float * cy);
// Distance from point p to segment ab
float geom_pointseg(float px, float py, float ax, float ay, float bx, float by);

#endif

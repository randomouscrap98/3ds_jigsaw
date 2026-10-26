#ifndef __HEADER_JIGSAW_CAMERA__
#define __HEADER_JIGSAW_CAMERA__

// A view onto the table: world point (cx, cy) shows at the screen center, scaled by zoom.
// The center is kept inside the table bounds.
typedef struct {
  float cx, cy;
  float zoom, minzoom, maxzoom;
  float sw, sh;   // Screen size
  float bw, bh;   // Table size (bounds are 0,0 to bw,bh)
} Camera;

// Starts centered on the table, zoomed out to fit the whole table
void camera_init(Camera * c, float sw, float sh, float bw, float bh, float maxzoom);
// Pan by screen pixels (positive dx moves the view right, i.e. the world slides left)
void camera_pan(Camera * c, float dx, float dy);
// Multiply zoom by factor, clamped, keeping the screen center fixed
void camera_zoom(Camera * c, float factor);
void camera_set(Camera * c, float cx, float cy, float zoom);
void camera_to_world(Camera * c, float sx, float sy, float * wx, float * wy);
void camera_to_screen(Camera * c, float wx, float wy, float * sx, float * sy);

#endif

#include "overlay.h"
#include "rainbow.h"

// Hologram: rainbow settings picked from previews (tools/rainbow_preview): corners mode,
// first palette
#define OVERLAY_HOLOGRAM_STRENGTH 0.55f
#define OVERLAY_HOLOGRAM_MINLIGHT 0.3f
#define OVERLAY_HOLOGRAM_MINDARK 0.1f
// Rainbow: diagonal mode, gentle (one minimum for light and dark)
#define OVERLAY_RAINBOW_STRENGTH 0.25f
#define OVERLAY_RAINBOW_MINIMUM 0.12f

void overlay_apply(uint8_t overlay, uint8_t * rgb, uint32_t w, uint32_t h) {
  if(overlay == OVERLAY_HOLOGRAM) {
    rainbow_apply(rgb, w, h, RAINBOW_CORNERS, 0, OVERLAY_HOLOGRAM_STRENGTH, OVERLAY_HOLOGRAM_MINLIGHT, OVERLAY_HOLOGRAM_MINDARK);
  } else if(overlay == OVERLAY_RAINBOW) {
    rainbow_apply(rgb, w, h, RAINBOW_DIAGONAL, 0, OVERLAY_RAINBOW_STRENGTH, OVERLAY_RAINBOW_MINIMUM, OVERLAY_RAINBOW_MINIMUM);
  }
}

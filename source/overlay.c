#include "overlay.h"
#include "rainbow.h"

// Hologram: rainbow settings picked from previews (tools/rainbow_preview): corners mode,
// first palette
#define OVERLAY_HOLOGRAM_STRENGTH 0.55f
#define OVERLAY_HOLOGRAM_MINLIGHT 0.3f
#define OVERLAY_HOLOGRAM_MINDARK 0.1f

void overlay_apply(uint8_t overlay, uint8_t * rgb, uint32_t w, uint32_t h) {
  if(overlay == OVERLAY_HOLOGRAM) {
    rainbow_apply(rgb, w, h, RAINBOW_CORNERS, 0, OVERLAY_HOLOGRAM_STRENGTH, OVERLAY_HOLOGRAM_MINLIGHT, OVERLAY_HOLOGRAM_MINDARK);
  }
}
